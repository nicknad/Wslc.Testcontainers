#include <gtest/gtest.h>

#include "internal/container_host.hpp"
#include "internal/instance_store.hpp"
#include "internal/util.hpp"
#include "support/integration.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/modules/clickhouse.hpp"
#include "wslc/modules/mailpit.hpp"
#include "wslc/modules/mariadb.hpp"
#include "wslc/modules/mongodb.hpp"
#include "wslc/modules/nats.hpp"
#include "wslc/modules/postgresql.hpp"
#include "wslc/modules/qdrant.hpp"
#include "wslc/modules/rabbitmq.hpp"
#include "wslc/modules/rustfs.hpp"
#include "wslc/modules/valkey.hpp"
#include "wslc/modules/wiremock.hpp"
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
    LogStream stream = container.SubscribeLogs();
    std::stop_source source;
    std::jthread stopper(
        [&source]
        {
            std::this_thread::sleep_for(3s);
            source.request_stop();
        });

    while (auto line = stream.Next(source.get_token()))
    {
        lines.push_back(*line);
    }

    return lines;
}

bool ContainsInsensitive(const std::vector<LogLine>& lines, const std::string& needle)
{
    const std::string loweredNeedle = wslc::internal::ToLower(needle);
    return std::any_of(lines.begin(), lines.end(), [&loweredNeedle](const LogLine& line)
                       { return wslc::internal::ToLower(line.Text).contains(loweredNeedle); });
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
        EXPECT_NE(result.Stdout.find("hello-wslc"), std::string::npos);
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
        EXPECT_EQ(result.Stdout, std::string("hello"));
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
        EXPECT_EQ(cat.Stdout, std::string("wslc-file-content"));

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
            .WithWaitStrategy(wslc::waiting::ForWsl().WithTimeout(60s).UntilHttpRequestSucceeds("/", 8080))
            .Build();
    try
    {
        container.Start();

        const wslc::WslEndpoint endpoint = container.GetConnectEndpoint(8080);
        EXPECT_GT(endpoint.Port, 0);
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
    auto container = builder.WithImage(TestImage).WithNetworkingMode(ContainerNetworkMode::Isolated).Build();
    try
    {
        container.Start();

        const auto result = container.Exec("/bin/sh", {"-c", "echo offline-ok"});

        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find("offline-ok"), std::string::npos);
        EXPECT_THROW(container.GetConnectEndpoint(8080), WslNetworkException);
    }
    catch (...)
    {
        container.Dispose();
        throw;
    }

    container.Dispose();
}

TEST(Integration, ScratchVolumesMountAndAreRecreatedOnRestart)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container = builder.WithImage(TestImage).WithScratchVolume("scratch", "/scratch", 64 * Megabyte).Build();
    try
    {
        container.Start();

        const auto write =
            container.Exec("/bin/sh", {"-c", "echo persisted > /scratch/data.txt && cat /scratch/data.txt"});
        EXPECT_EQ(write.ExitCode, 0);
        EXPECT_EQ(write.Stdout.substr(0, write.Stdout.find('\n')), std::string("persisted"));

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
    const std::string previous = GetEnvironment(wslc::WslEnvironment::ReuseInCiVariable);
    _putenv_s(wslc::WslEnvironment::ReuseInCiVariable, "1");
    try
    {
        // A per-run environment value makes the configuration hash unique, so the first start
        // is guaranteed to pull instead of hitting a leftover cache.
        const std::string runMarker = RandomHex(32);
        WslContainerBuilder firstBuilder;
        firstBuilder.WithImage(TestImage)
            .WithEnvironment("WSLC_REUSE_TEST_RUN", runMarker)
            .WithScratchVolume("scratch", "/scratch", 64 * Megabyte)
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
                .WithScratchVolume("scratch", "/scratch", 64 * Megabyte)
                .WithReuse();
            auto second = secondBuilder.Build();
            EXPECT_EQ(second.Name(), name);
            second.Start();
            try
            {
                EXPECT_TRUE(second.IsStarted());
                EXPECT_FALSE(ContainsInsensitive(ReadLogHistory(second), "pulling image"));

                // The scratch volume is recreated empty even though the session VHD persists.
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
        _putenv_s(wslc::WslEnvironment::ReuseInCiVariable, previous.c_str());
        WslResourceReaper::PurgeReuse();
        throw;
    }

    _putenv_s(wslc::WslEnvironment::ReuseInCiVariable, previous.c_str());
    WslResourceReaper::PurgeReuse();
}

TEST(Integration, ReadOnlyScratchVolumesRejectWrites)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    WslContainerBuilder builder;
    auto container =
        builder.WithImage(TestImage)
            .WithScratchVolume("scratch", "/scratch", 64 * Megabyte, VolumeAccess::ReadOnly, VhdAllocationType::Fixed)
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
    auto container = builder.WithImage(TestImage).WithCpuCount(cpuCount).WithMemoryMegabytes(1024).Build();
    try
    {
        container.Start();

        const auto cpus = container.Exec("nproc");
        EXPECT_EQ(std::stoul(cpus.Stdout), cpuCount);

        const auto memory = container.Exec("/bin/sh", {"-c", "awk '/MemTotal/ {print $2}' /proc/meminfo"});
        const long memoryKb = std::stol(memory.Stdout);
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
        EXPECT_EQ(result.Stdout, std::string("mounted"));
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
        EXPECT_NE(result.Stdout.find('1'), std::string::npos);
    }
    catch (...)
    {
        postgres.Dispose();
        throw;
    }

    postgres.Dispose();
}

TEST(IntegrationModules, ValkeyModuleStartsAndAnswersPing)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::ValkeyBuilder builder;
    auto valkey = builder.Build();
    try
    {
        valkey.Start();

        EXPECT_NE(valkey.GetEndpoint().find("127.0.0.1:"), std::string::npos);
        const auto result = valkey.Exec("/bin/sh", {"-c", "valkey-cli ping"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find("PONG"), std::string::npos);
    }
    catch (...)
    {
        valkey.Dispose();
        throw;
    }

    valkey.Dispose();
}

TEST(IntegrationModules, MariaDbModuleStartsAndServesQueries)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::MariaDbBuilder builder;
    auto mariadb = builder.Build();
    try
    {
        mariadb.Start();

        EXPECT_NE(mariadb.GetConnectionString().find("Server=127.0.0.1"), std::string::npos);
        const auto result =
            mariadb.Exec("mariadb", {"-u", "mariadb", "--password=secret", "-D", "customers", "-e", "SELECT 1"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find('1'), std::string::npos);
    }
    catch (...)
    {
        mariadb.Dispose();
        throw;
    }

    mariadb.Dispose();
}

TEST(IntegrationModules, RabbitMqModuleStartsAndAnswersPing)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::RabbitMqBuilder builder;
    auto rabbitmq = builder.Build();
    try
    {
        rabbitmq.Start();

        EXPECT_NE(rabbitmq.GetConnectionString().find("amqp://rabbit:secret@127.0.0.1:"), std::string::npos);
        const auto result = rabbitmq.Exec("rabbitmq-diagnostics", {"-q", "ping"});
        EXPECT_EQ(result.ExitCode, 0);
    }
    catch (...)
    {
        rabbitmq.Dispose();
        throw;
    }

    rabbitmq.Dispose();
}

TEST(IntegrationModules, MongoDbModuleStartsAndServesPing)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::MongoDbBuilder builder;
    auto mongodb = builder.Build();
    try
    {
        mongodb.Start();

        EXPECT_NE(mongodb.GetConnectionString().find("mongodb://127.0.0.1:"), std::string::npos);
        const auto result = mongodb.Exec("mongosh", {"--quiet", "--eval", "db.adminCommand({ping:1}).ok"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find('1'), std::string::npos);
    }
    catch (...)
    {
        mongodb.Dispose();
        throw;
    }

    mongodb.Dispose();
}

TEST(IntegrationModules, NatsModuleStartsWithJetStream)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::NatsBuilder builder;
    builder.WithJetStream();
    auto nats = builder.Build();
    try
    {
        nats.Start();

        EXPECT_NE(nats.GetConnectionString().find("nats://127.0.0.1:"), std::string::npos);
        const auto result = nats.Exec("/bin/sh", {"-c", "printf 'PING\\r\\n' | nc -w 1 127.0.0.1 4222"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find("PONG"), std::string::npos);
    }
    catch (...)
    {
        nats.Dispose();
        throw;
    }

    nats.Dispose();
}

TEST(IntegrationModules, MailPitModuleStartsAndAnswersHealth)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::MailPitBuilder builder;
    auto mailpit = builder.Build();
    try
    {
        mailpit.Start();

        EXPECT_NE(mailpit.GetSmtpEndpoint().find("127.0.0.1:"), std::string::npos);
        EXPECT_NE(mailpit.GetHttpEndpoint().find("http://127.0.0.1:"), std::string::npos);
        const auto result = mailpit.Exec("/bin/sh", {"-c", "wget -q -O - http://127.0.0.1:8025/livez"});
        EXPECT_EQ(result.ExitCode, 0);
    }
    catch (...)
    {
        mailpit.Dispose();
        throw;
    }

    mailpit.Dispose();
}

TEST(IntegrationModules, RustFsModuleStartsAndAnswersHealth)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::RustFsBuilder builder;
    auto rustfs = builder.Build();
    try
    {
        rustfs.Start();

        EXPECT_NE(rustfs.GetEndpoint().find("http://127.0.0.1:"), std::string::npos);
        EXPECT_EQ(rustfs.AccessKey(), "rustfsadmin");
        const auto result = rustfs.Exec("/bin/sh", {"-c", "wget -q -O - http://127.0.0.1:9000/health"});
        EXPECT_EQ(result.ExitCode, 0);
    }
    catch (...)
    {
        rustfs.Dispose();
        throw;
    }

    rustfs.Dispose();
}

TEST(IntegrationModules, WireMockModuleStartsAndAnswersHealth)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::WireMockBuilder builder;
    auto wiremock = builder.Build();
    try
    {
        wiremock.Start();

        EXPECT_NE(wiremock.GetEndpoint().find("http://127.0.0.1:"), std::string::npos);
        const auto result = wiremock.Exec("/bin/sh", {"-c", "wget -q -O - http://127.0.0.1:8080/__admin/health"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find("healthy"), std::string::npos);
    }
    catch (...)
    {
        wiremock.Dispose();
        throw;
    }

    wiremock.Dispose();
TEST(IntegrationModules, QdrantModuleStartsAndAnswersReady)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::QdrantBuilder builder;
    auto qdrant = builder.Build();
    try
    {
        qdrant.Start();

        EXPECT_NE(qdrant.GetEndpoint().find("http://127.0.0.1:"), std::string::npos);
        // The image ships no HTTP client; bash's /dev/tcp is the verified in-container check.
        const auto result =
            qdrant.Exec("/bin/bash", {"-c", "exec 3<>/dev/tcp/127.0.0.1/6333; printf 'GET /readyz HTTP/1.0\\r\\nHost: "
                                            "127.0.0.1\\r\\n\\r\\n' >&3; head -n 1 <&3"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find("200 OK"), std::string::npos);
    }
    catch (...)
    {
        qdrant.Dispose();
        throw;
    }

    qdrant.Dispose();
TEST(IntegrationModules, ClickHouseModuleStartsAndServesQuery)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::modules::ClickHouseBuilder builder;
    auto clickhouse = builder.Build();
    try
    {
        clickhouse.Start();

        EXPECT_NE(clickhouse.GetConnectionString().find("Host=127.0.0.1;Port="), std::string::npos);
        const auto result = clickhouse.Exec("clickhouse-client", {"--query", "SELECT 1"});
        EXPECT_EQ(result.ExitCode, 0);
        EXPECT_NE(result.Stdout.find('1'), std::string::npos);
    }
    catch (...)
    {
        clickhouse.Dispose();
        throw;
    }

    clickhouse.Dispose();
}
