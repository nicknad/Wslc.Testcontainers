#include <gtest/gtest.h>

#include "internal/container_host.hpp"
#include "internal/instance_store.hpp"
#include "internal/util.hpp"
#include "support/integration.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/modules/postgresql.hpp"
#include "wslc/wslc.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using wslc::ContainerNetworkMode;
using wslc::LogLine;
using wslc::LogStream;
using wslc::VhdAllocationType;
using wslc::VolumeAccess;
using wslc::WslContainer;
using wslc::WslContainerBuilder;
using wslc::WslNetworkException;
using wslc::WslReadinessException;
using wslc::WslResourceReaper;
using wslc::internal::InstanceStore;
using wslc::internal::RandomHex;

namespace
{

constexpr const char* TestImage = "docker.io/library/alpine:latest";
constexpr unsigned long long Megabyte = 1024ull * 1024ull;

std::vector<LogLine> ReadLogHistory(WslContainer& container)
{
    std::vector<LogLine> lines;
    LogStream stream = container.Logs();
    std::stop_source source;
    std::thread stopper(
        [&source]
        {
            std::this_thread::sleep_for(3s);
            source.request_stop();
        });

    while (auto line = stream.Next(source.get_token()))
    {
        lines.push_back(*line);
    }

    stopper.join();
    return lines;
}

bool ContainsInsensitive(const std::vector<LogLine>& lines, const std::string& needle)
{
    const std::string loweredNeedle = wslc::internal::ToLower(needle);
    return std::any_of(lines.begin(), lines.end(), [&loweredNeedle](const LogLine& line)
                       { return wslc::internal::ToLower(line.Text).find(loweredNeedle) != std::string::npos; });
}

std::filesystem::path CreateTempFile(const std::string& content)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("wslc-" + RandomHex(16) + ".txt");
    std::ofstream(path) << content;
    return path;
}

std::string GetEnvironment(const char* name)
{
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, name);
    const std::string result = value != nullptr ? value : "";
    free(value);
    return result;
}

} // namespace

TEST(Integration, RuntimeComponentsAreAvailable)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    EXPECT_NO_THROW(wslc::internal::WslcHost::EnsureAvailable());
}

TEST(Integration, RunsCommandsAndCapturesOutput)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).Build();
    try
    {
        container.Start();

        const auto result = container.Exec("/bin/sh", {"-c", "echo hello-wslc"});

        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.StdoutText.find("hello-wslc"), std::string::npos);
        EXPECT_TRUE(container.IsStarted());
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, ExposesEnvironmentVariables)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithEnvironment("WSLC_TEST_VALUE", "hello").Build();
    try
    {
        container.Start();

        const auto result = container.Exec("/bin/sh", {"-c", "printf %s \"$WSLC_TEST_VALUE\""});

        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_EQ(result.StdoutText, std::string("hello"));
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, CopiesFilesIntoAndOutOfTheContainer)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    const std::filesystem::path source = CreateTempFile("wslc-file-content");
    const std::filesystem::path destination = source.string() + ".out";

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).Build();
    try
    {
        container.Start();

        container.CopyTo(source, "/tmp/wslc-test.txt");
        const auto cat = container.Exec("/bin/cat", {"/tmp/wslc-test.txt"});
        EXPECT_EQ(cat.StdoutText, std::string("wslc-file-content"));

        container.CopyFrom("/tmp/wslc-test.txt", destination);
        std::ifstream output(destination);
        std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, std::string("wslc-file-content"));
    }
    catch (...)
    {
        container.Dispose();
        std::filesystem::remove(source);
        std::filesystem::remove(destination);
        throw;
    }

    container.Dispose();
    std::filesystem::remove(source);
    std::filesystem::remove(destination);
}

TEST(Integration, MapsPortsAndServesHttp)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container =
        builder.WithImage(TestImage)
            .WithCommand("/bin/sh", {"-c", "while true; do printf 'HTTP/1.1 200 OK\\r\\nContent-Length: "
                                           "2\\r\\nConnection: close\\r\\n\\r\\nok' | nc -l -p 8080; done"})
            .WithPort(8080)
            .WithWaitStrategy(wslc::waiting::ForWsl().WithTimeout(60s).UntilHttpRequestIsSucceeded("/", 8080))
            .Build();
    try
    {
        container.Start();

        const int mappedPort = container.GetMappedPort(8080);
        EXPECT_GT(mappedPort, 0);
        EXPECT_TRUE(container.IsStarted());
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, ReadinessFailureCleansUpEphemeralStorage)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container =
        builder.WithImage(TestImage)
            .WithPort(65000)
            .WithWaitStrategy(wslc::waiting::ForWsl().WithTimeout(5s).WithRetryInterval(200ms).UntilMessageIsLogged(
                "this-message-never-appears"))
            .Build();
    const std::string name = container.Name();

    EXPECT_THROW(container.Start(), WslReadinessException);
    EXPECT_FALSE(std::filesystem::exists(InstanceStore::DefaultStore().GetInstanceDirectory(name)));

    container.Dispose();
}

TEST(Integration, IsolatedNetworkingRunsCommandsWithoutPorts)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithNetworkingMode(ContainerNetworkMode::None).Build();
    try
    {
        container.Start();

        const auto result = container.Exec("/bin/sh", {"-c", "echo offline-ok"});

        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.StdoutText.find("offline-ok"), std::string::npos);
        EXPECT_THROW(container.GetMappedPort(8080), WslNetworkException);
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, SessionVolumesMountAndAreRecreatedOnRestart)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithSessionVolume("scratch", "/scratch", 64 * Megabyte).Build();
    try
    {
        container.Start();

        const auto write =
            container.Exec("/bin/sh", {"-c", "echo persisted > /scratch/data.txt && cat /scratch/data.txt"});
        EXPECT_EQ(write.ExitCode, 0);
        EXPECT_EQ(write.StdoutText.substr(0, write.StdoutText.find('\n')), std::string("persisted"));

        container.Stop();
        container.Start();

        const auto exists = container.Exec("/bin/sh", {"-c", "test -e /scratch/data.txt"});
        EXPECT_NE(exists.ExitCode, 0);
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, ReuseKeepsTheSessionVhdAndImageCache)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    // Reuse is forced off under CI unless WSLC_REUSE_IN_CI is set; the integration pipeline
    // runs with GITHUB_ACTIONS=1, so opt in for this test only.
    const std::string previous = GetEnvironment(wslc::WslcEnvironment::ReuseInCiVariable);
    _putenv_s(wslc::WslcEnvironment::ReuseInCiVariable, "1");
    try
    {
        // A per-run environment value makes the configuration hash unique, so the first start
        // is guaranteed to pull instead of hitting a leftover cache.
        const std::string runMarker = RandomHex(32);
        WslContainerBuilder firstBuilder;
        firstBuilder.WithImage(TestImage)
            .WithEnvironment("WSLC_REUSE_TEST_RUN", runMarker)
            .WithSessionVolume("scratch", "/scratch", 64 * Megabyte)
            .WithReuse();
        auto first = firstBuilder.Build();
        const std::string name = first.Name();
        const std::filesystem::path storageDirectory = InstanceStore::DefaultStore().GetSessionStorageDirectory(name);
        try
        {
            first.Start();
            EXPECT_TRUE(first.IsStarted());
            EXPECT_TRUE(ContainsInsensitive(ReadLogHistory(first), "pulling image"));

            const auto write = first.Exec("/bin/sh", {"-c", "echo persisted > /scratch/data.txt"});
            EXPECT_EQ(write.ExitCode, 0);

            first.Dispose();

            // Dispose keeps reuse storage, so the session VHD (image cache) must survive.
            EXPECT_TRUE(std::filesystem::exists(storageDirectory / "storage.vhdx"));

            WslContainerBuilder secondBuilder;
            secondBuilder.WithImage(TestImage)
                .WithEnvironment("WSLC_REUSE_TEST_RUN", runMarker)
                .WithSessionVolume("scratch", "/scratch", 64 * Megabyte)
                .WithReuse();
            auto second = secondBuilder.Build();
            EXPECT_EQ(second.Name(), name);
            second.Start();
            try
            {
                EXPECT_TRUE(second.IsStarted());
                EXPECT_FALSE(ContainsInsensitive(ReadLogHistory(second), "pulling image"));

                // The session volume is scratch: recreated empty even though the session VHD persists.
                const auto exists = second.Exec("/bin/sh", {"-c", "test -e /scratch/data.txt"});
                EXPECT_NE(exists.ExitCode, 0);
            }
            catch (...)
            {
                second.Dispose();
                throw;
            }

            second.Dispose();
        }
        catch (...)
        {
            first.Dispose();
            throw;
        }
    }
    catch (...)
    {
        _putenv_s(wslc::WslcEnvironment::ReuseInCiVariable, previous.c_str());
        WslResourceReaper::PurgeReuse();
        throw;
    }

    _putenv_s(wslc::WslcEnvironment::ReuseInCiVariable, previous.c_str());
    WslResourceReaper::PurgeReuse();
}

TEST(Integration, ReadOnlySessionVolumesRejectWrites)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container =
        builder.WithImage(TestImage)
            .WithSessionVolume("scratch", "/scratch", 64 * Megabyte, VolumeAccess::ReadOnly, VhdAllocationType::Fixed)
            .Build();
    try
    {
        container.Start();

        const auto mount = container.Exec("/bin/sh", {"-c", "test -d /scratch"});
        EXPECT_EQ(mount.ExitCode, 0);

        const auto write = container.Exec("/bin/sh", {"-c", "echo nope > /scratch/data.txt"});
        EXPECT_NE(write.ExitCode, 0);
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, ResourceCapsAreAppliedToTheSession)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    const unsigned int cpuCount = std::min(2u, std::thread::hardware_concurrency());
    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithCpuCount(cpuCount).WithMemoryMB(1024).Build();
    try
    {
        container.Start();

        const auto cpus = container.Exec("nproc");
        EXPECT_EQ(std::stoul(cpus.StdoutText), cpuCount);

        const auto memory = container.Exec("/bin/sh", {"-c", "awk '/MemTotal/ {print $2}' /proc/meminfo"});
        const long memoryKb = std::stol(memory.StdoutText);
        EXPECT_GE(memoryKb, 300'000);
        EXPECT_LE(memoryKb, 1'500'000);
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, MountsWindowsDirectoriesAsVolumes)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    const std::filesystem::path hostDirectory =
        std::filesystem::temp_directory_path() / ("wslc-volume-" + RandomHex(16));
    std::filesystem::create_directories(hostDirectory);
    std::ofstream(hostDirectory / "data.txt") << "mounted";

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithVolume(hostDirectory, "/workspace").Build();
    try
    {
        container.Start();

        const auto result = container.Exec("/bin/cat", {"/workspace/data.txt"});

        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_EQ(result.StdoutText, std::string("mounted"));
    }
    catch (...)
    {
        container.Dispose();
        std::filesystem::remove_all(hostDirectory);
        throw;
    }

    container.Dispose();
    std::filesystem::remove_all(hostDirectory);
}

TEST(IntegrationModules, PostgreSqlModuleStartsAndServesQueries)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::PostgreSqlBuilder builder;
    builder.WithPassword("secret");
    auto postgres = builder.Build();
    try
    {
        postgres.Start();

        EXPECT_NE(postgres.GetConnectionString().find("Host=127.0.0.1"), std::string::npos);
        const auto result =
            postgres.Exec("/bin/sh", {"-c", "PGPASSWORD=secret psql -U postgres -d customers -tAc 'SELECT 1'"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.StdoutText.find('1'), std::string::npos);
    }
    catch (...)
    {
        postgres.Dispose();
        throw;
    }

    postgres.Dispose();
}
