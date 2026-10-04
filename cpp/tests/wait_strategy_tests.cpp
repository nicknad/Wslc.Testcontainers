#include <gtest/gtest.h>

#include "support/fake_wait_target.hpp"
#include "support/tiny_http_server.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"

#include <algorithm>
#include <chrono>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using wslc::ExecResult;
using wslc::LogLine;
using wslc::LogSource;
using wslc::OperationCanceledException;
using wslc::WslcException;
using wslc::WslReadinessException;
using wslc::test::FakeWaitTarget;
using wslc::test::TinyHttpServer;
using wslc::waiting::ForWsl;

TEST(WaitStrategy, TcpPortPollsUntilAvailable)
{
    int attempts = 0;
    FakeWaitTarget target;
    target.PortHandler = [&attempts](int, std::stop_token)
    {
        attempts++;
        return attempts >= 3;
    };

    auto strategy = ForWsl().WithTimeout(5s).WithRetryInterval(10ms).UntilTcpPortIsAvailable(5432);

    strategy->Wait(target, std::stop_token{});

    EXPECT_GE(attempts, 3);
}

TEST(WaitStrategy, TcpPortTimesOutWithDiagnostics)
{
    FakeWaitTarget target;
    target.PortHandler = [](int, std::stop_token) { return false; };
    target.Logs.push_back(LogLine::Diagnostic("database starting"));

    auto strategy = ForWsl().WithTimeout(120ms).WithRetryInterval(10ms).UntilTcpPortIsAvailable(5432);

    try
    {
        strategy->Wait(target, std::stop_token{});
        FAIL() << "Expected a WslReadinessException";
    }
    catch (const WslReadinessException& exception)
    {
        EXPECT_NE(exception.ExpectedCondition().find("TCP port 5432"), std::string::npos);
        EXPECT_EQ(exception.Timeout(), 120ms);
        bool found = false;
        for (const auto& line : exception.Logs())
        {
            found = found || line.Text == "database starting";
        }

        EXPECT_TRUE(found);
    }
}

TEST(WaitStrategy, HonorsCancellation)
{
    FakeWaitTarget target;
    target.PortHandler = [](int, std::stop_token) { return false; };

    auto strategy = ForWsl().WithTimeout(30s).WithRetryInterval(10ms).UntilTcpPortIsAvailable(5432);

    std::stop_source source;
    std::thread canceller(
        [&source]
        {
            std::this_thread::sleep_for(100ms);
            source.request_stop();
        });

    EXPECT_THROW(strategy->Wait(target, source.get_token()), OperationCanceledException);

    canceller.join();
}

TEST(WaitStrategy, ProcessRunningAndExitUseTheTarget)
{
    bool running = true;
    FakeWaitTarget target;
    target.ProcessHandler = [&running](std::string, std::stop_token) { return running; };

    auto waitUntilRunning = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).UntilProcessIsRunning("postgres");
    waitUntilRunning->Wait(target, std::stop_token{});

    running = false;
    auto waitUntilExited = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).UntilProcessExits("postgres");
    waitUntilExited->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, FileExistsExecutesTestCommand)
{
    FakeWaitTarget target;
    target.ExecHandler = [](std::string command, std::vector<std::string> arguments, std::stop_token)
    {
        const bool matched =
            command == "test" && std::find(arguments.begin(), arguments.end(), "/tmp/ready") != arguments.end();
        return ExecResult{matched ? 0 : 1, "", ""};
    };

    auto strategy = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).UntilFileExists("/tmp/ready");

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, LogMessageMatchesCapturedLogs)
{
    FakeWaitTarget target;
    target.Logs.push_back(LogLine{LogSource::Stdout, "database system is ready to accept connections", {}});

    auto strategy = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).UntilMessageIsLogged("database system is ready");

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, LogMessageCanRequireMultipleOccurrences)
{
    FakeWaitTarget target;
    target.Logs.push_back(LogLine{LogSource::Stdout, "ready", {}});

    auto strategy = ForWsl().WithTimeout(250ms).WithRetryInterval(10ms).UntilMessageIsLogged("ready", 2);

    EXPECT_THROW(strategy->Wait(target, std::stop_token{}), WslReadinessException);

    target.Logs.push_back(LogLine{LogSource::Stdout, "ready", {}});
    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, LogMessageOccurrencesAreCountedWithinASingleLine)
{
    FakeWaitTarget target;
    target.Logs.push_back(LogLine{LogSource::Stdout, "ready ready", {}});

    auto strategy = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).UntilMessageIsLogged("ready", 2);

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, LogMessageIgnoresDiagnostics)
{
    FakeWaitTarget target;
    target.Logs.push_back(LogLine::Diagnostic("never-appears"));

    auto strategy = ForWsl().WithTimeout(200ms).WithRetryInterval(10ms).UntilMessageIsLogged("never-appears");

    EXPECT_THROW(strategy->Wait(target, std::stop_token{}), WslReadinessException);
}

TEST(WaitStrategy, CompositeRequiresAllConditions)
{
    FakeWaitTarget target;
    target.PortHandler = [](int, std::stop_token) { return true; };
    target.ProcessHandler = [](std::string, std::stop_token) { return true; };
    target.Logs.push_back(LogLine{LogSource::Stdout, "ready", {}});

    auto strategy = ForWsl()
                        .WithTimeout(2s)
                        .WithRetryInterval(10ms)
                        .UntilTcpPortIsAvailable(8080)
                        ->And(ForWsl().UntilProcessIsRunning("nginx"))
                        ->And(ForWsl().UntilMessageIsLogged("ready"));

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, HttpSucceedsForNonServerErrors)
{
    TinyHttpServer server(200);
    FakeWaitTarget target;
    target.MappedPort = server.Port();

    auto strategy = ForWsl().WithTimeout(5s).WithRetryInterval(50ms).UntilHttpRequestIsSucceeded("/health", 8080);

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, HttpProbesTheMappedPortsHost)
{
    TinyHttpServer server(200);
    FakeWaitTarget target;
    target.HostAddress = "192.0.2.1";
    target.ProbeHostAddress = "127.0.0.1";
    target.MappedPort = server.Port();

    auto strategy = ForWsl().WithTimeout(5s).WithRetryInterval(50ms).UntilHttpRequestIsSucceeded("/health", 8080);

    strategy->Wait(target, std::stop_token{});
}

TEST(WaitStrategy, HttpTimesOutForServerErrors)
{
    TinyHttpServer server(500);
    FakeWaitTarget target;
    target.MappedPort = server.Port();

    auto strategy = ForWsl().WithTimeout(300ms).WithRetryInterval(25ms).UntilHttpRequestIsSucceeded("/health", 8080);

    EXPECT_THROW(strategy->Wait(target, std::stop_token{}), WslReadinessException);
}

TEST(WaitStrategy, CustomUntilPollsUntilSatisfied)
{
    int attempts = 0;
    auto strategy = ForWsl().WithTimeout(2s).WithRetryInterval(10ms).Until(
        "custom flag", [&attempts](wslc::waiting::IWaitTarget&, std::stop_token) { return ++attempts >= 3; });

    FakeWaitTarget target;
    strategy->Wait(target, std::stop_token{});

    EXPECT_GE(attempts, 3);
    EXPECT_EQ(strategy->Name(), std::string("custom flag"));
}

TEST(WaitStrategy, CompositeKeepsTheLeftOperandTimeoutAndRetryInterval)
{
    auto strategy = ForWsl().WithTimeout(150ms).WithRetryInterval(25ms).UntilTcpPortIsAvailable(8080)->And(
        ForWsl().WithTimeout(30s).UntilProcessIsRunning("nginx"));

    EXPECT_EQ(strategy->Timeout(), 150ms);
    EXPECT_EQ(strategy->RetryInterval(), 25ms);
}

TEST(WaitStrategy, CustomUntilValidatesArguments)
{
    EXPECT_THROW(ForWsl().Until(" ", [](wslc::waiting::IWaitTarget&, std::stop_token) { return true; }), WslcException);
    EXPECT_THROW(ForWsl().Until("condition", std::function<bool(wslc::waiting::IWaitTarget&, std::stop_token)>()),
                 WslcException);
}

TEST(WaitStrategy, InvalidConfigurationIsRejected)
{
    EXPECT_THROW(ForWsl().WithTimeout(0ms), WslcException);
    EXPECT_THROW(ForWsl().WithRetryInterval(0ms), WslcException);
    EXPECT_THROW(ForWsl().UntilTcpPortIsAvailable(0), WslcException);
    EXPECT_THROW(ForWsl().UntilFileExists(" "), WslcException);

    // HTTP waits take a path-and-query, never a full URL or a relative path.
    EXPECT_THROW(ForWsl().UntilHttpRequestIsSucceeded("http://localhost/health", 8080), WslcException);
    EXPECT_THROW(ForWsl().UntilHttpRequestIsSucceeded("health", 8080), WslcException);

    EXPECT_THROW(ForWsl().UntilMessageIsLogged("ready", 0), WslcException);
    EXPECT_THROW(ForWsl().UntilMessageIsLogged(" ", 2), WslcException);
}
