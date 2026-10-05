#include <gtest/gtest.h>

#include <windows.h>

#include "internal/limits.hpp"
#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exec.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/modules/postgresql.hpp"
#include "wslc/waiting/wait.hpp"
#include "wslc/wsl_container_builder.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>

using namespace std::chrono_literals;
using wslc::ContainerNetworkMode;
using wslc::VhdAllocationType;
using wslc::VolumeAccess;
using wslc::WslContainerBuilder;
using wslc::WslException;
using wslc::waiting::ForWsl;

namespace
{

std::filesystem::path CreateTempFile(const std::string& content)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "wslc-test-input.txt";
    std::ofstream(path) << content;
    return path;
}

class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const wchar_t* Name, const wchar_t* value) : m_name(Name)
    {
        const DWORD length = GetEnvironmentVariableW(Name, nullptr, 0);
        if (length > 0)
        {
            std::wstring buffer(length, L'\0');
            const DWORD written = GetEnvironmentVariableW(Name, buffer.data(), length);
            buffer.resize(written);
            m_previous = std::move(buffer);
        }

        SetEnvironmentVariableW(Name, value);
    }

    ~ScopedEnvironmentVariable()
    {
        SetEnvironmentVariableW(m_name.c_str(), m_previous.empty() ? nullptr : m_previous.c_str());
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

private:
    std::wstring m_name;
    std::wstring m_previous;
};

} // namespace

TEST(ContainerBuilder, BuildRequiresAnImageSource)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, FromTarballRejectsAMissingTarball)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.FromTarball("does-not-exist.tar"), WslException);
}

TEST(ContainerBuilder, WithImageRecordsTheImage)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    EXPECT_EQ(container.Image().value_or(""), std::string("alpine:latest"));
    EXPECT_EQ(container.Name().rfind("wslc-", 0), 0u);
}

TEST(ContainerBuilder, FromTarballRecordsTheOptionalImageName)
{
    const std::filesystem::path path = CreateTempFile("not-a-real-tarball");
    try
    {
        WslContainerBuilder builder;
        auto container = builder.FromTarball(path, "custom:local").Build();

        EXPECT_EQ(container.Image().value_or(""), std::string("custom:local"));
    }
    catch (...)
    {
        std::filesystem::remove(path);
        throw;
    }

    std::filesystem::remove(path);
}

TEST(ContainerBuilder, BuildersMutateInPlaceAndBuildSnapshotsConfiguration)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine:latest").WithEnvironment("A", "1").WithReuse(true);

    auto first = builder.Build();
    builder.WithCommand("redis-server");
    auto second = builder.Build();

    EXPECT_NE(first.Name(), second.Name());
    EXPECT_EQ(&builder.WithCommand("nginx"), &builder);
    auto third = builder.Build();
    EXPECT_NE(second.Name(), third.Name());
}

TEST(ContainerBuilder, IsReuseEffectiveReportsConfiguredReuse)
{
    WslContainerBuilder configured;
    configured.WithImage("alpine:latest").WithReuse(true);
    const auto configuredContainer = configured.Build();
    EXPECT_EQ(configuredContainer.IsReuseEffective(),
              wslc::internal::IsReuseEffective(true, wslc::WslEnvironment::ReuseByDefault(),
                                               wslc::WslEnvironment::ReuseAllowed()));

    WslContainerBuilder ephemeral;
    ephemeral.WithImage("alpine:latest").WithReuse(false);
    EXPECT_FALSE(ephemeral.Build().IsReuseEffective());
}

TEST(ContainerBuilder, WithPortValidatesAndDeduplicates)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithPort(8080).WithPort(8080).WithPort(5432).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithPort(0), WslException);
    EXPECT_THROW(WslContainerBuilder{}.WithPort(70000), WslException);
}

TEST(ContainerBuilder, WithPortSupportsBindAddresses)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithPort(8080).WithPort(9090, "127.0.0.1").Build());
}

TEST(ContainerBuilder, WithPortRejectsInvalidBindAddressesAndConflicts)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine");
    EXPECT_THROW(builder.WithPort(8080, "not-an-ip"), WslException);
    EXPECT_THROW(builder.WithPort(8080, ""), WslException);

    builder.WithPort(8080, "127.0.0.1");
    EXPECT_THROW(builder.WithPort(8080, "0.0.0.0"), WslException);
}

TEST(ContainerBuilder, WithPortNormalizesTheBindAddress)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine").WithPort(8080, "0:0:0:0:0:0:0:1");

    // The IPv6 loopback spelled differently normalizes to the same address, so it must not
    // be rejected as a conflicting declaration.
    EXPECT_NO_THROW(builder.WithPort(8080, "::1").Build());
}

TEST(ContainerBuilder, WithCpuCountAndMemoryMegabytesRecordLimits)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithCpuCount(2).WithMemoryMegabytes(2048).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithCpuCount(0), WslException);
    EXPECT_THROW(WslContainerBuilder{}.WithMemoryMegabytes(0), WslException);
}

TEST(ContainerBuilder, WithNetworkingModeIsolatedRejectsPortsAndNetworkWaits)
{
    WslContainerBuilder ports;
    ports.WithImage("alpine").WithPort(8080).WithNetworkingMode(ContainerNetworkMode::Isolated);
    EXPECT_THROW(ports.Build(), WslException);

    WslContainerBuilder waits;
    waits.WithImage("alpine")
        .WithWaitStrategy(ForWsl().UntilTcpPortIsOpen(80))
        .WithNetworkingMode(ContainerNetworkMode::Isolated);
    EXPECT_THROW(waits.Build(), WslException);

    WslContainerBuilder composite;
    composite.WithImage("alpine")
        .WithWaitStrategy(
            ForWsl().UntilMessageIsLogged("ready")->And(ForWsl().UntilHttpRequestSucceeds("/health", 8080)))
        .WithNetworkingMode(ContainerNetworkMode::Isolated);
    EXPECT_THROW(composite.Build(), WslException);

    // Detection covers only built-in TCP/HTTP waits; a custom condition is not inspected and can
    // still be combined with None (documented bypass).
    WslContainerBuilder custom;
    custom.WithImage("alpine")
        .WithWaitStrategy(ForWsl().Until("custom", [](wslc::waiting::IWaitTarget&, std::stop_token) { return true; }))
        .WithNetworkingMode(ContainerNetworkMode::Isolated);
    EXPECT_NO_THROW(custom.Build());

    // Non-network waits are fine without networking.
    WslContainerBuilder offline;
    offline.WithImage("alpine")
        .WithWaitStrategy(ForWsl().UntilFileExists("/tmp/ready"))
        .WithNetworkingMode(ContainerNetworkMode::Isolated);
    EXPECT_NO_THROW(offline.Build());
}

TEST(ContainerBuilder, WithNetworkingModeRejectsUnknownValues)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithNetworkingMode(static_cast<ContainerNetworkMode>(99)), WslException);
}

TEST(ContainerBuilder, WithScratchVolumeRecordsAndValidates)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithScratchVolume("data", "/data", 10ull * 1024 * 1024 * 1024).Build());

    WslContainerBuilder invalid;
    invalid.WithImage("alpine");
    EXPECT_THROW(invalid.WithScratchVolume("", "/data", 100), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("a/b", "/data", 100), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("a b", "/data", 100), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("data", "relative", 100), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("data", "/data", 0), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("data", "/a", 100).WithScratchVolume("data", "/b", 100), WslException);
    EXPECT_THROW(invalid.WithScratchVolume("Data", "/a", 100).WithScratchVolume("data", "/b", 100), WslException);

    WslContainerBuilder readOnlyFixed;
    EXPECT_NO_THROW(readOnlyFixed.WithImage("alpine")
                        .WithScratchVolume("data", "/data", 100, VolumeAccess::ReadOnly, VhdAllocationType::Fixed)
                        .Build());
}

TEST(ContainerBuilder, WithEnvironmentRejectsInvalidNames)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithEnvironment("", "value"), WslException);
    EXPECT_THROW(builder.WithEnvironment("1INVALID", "value"), WslException);
    EXPECT_THROW(builder.WithEnvironment("HAS-DASH", "value"), WslException);
}

TEST(ContainerBuilder, WithEnvironmentRejectsNonAsciiNames)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine");

    EXPECT_THROW(builder.WithEnvironment("café", "value"), WslException);
    EXPECT_THROW(builder.WithEnvironment("Ωmega", "value"), WslException);
    EXPECT_THROW(builder.WithEnvironment("Aé", "value"), WslException);

    EXPECT_NO_THROW(WslContainerBuilder{}.WithImage("alpine").WithEnvironment("A_B", "value").Build());
    EXPECT_NO_THROW(WslContainerBuilder{}.WithImage("alpine").WithEnvironment("_x1", "value").Build());
}

TEST(ContainerBuilder, WithFileRequiresAnExistingFile)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithFile("missing.txt", "/tmp/missing.txt"), WslException);
}

TEST(ContainerBuilder, WithFileAbsolutizesTheHostPathAtBuildTime)
{
    // Reuse names are hashes of the stored configuration, which includes the host file path;
    // forcing reuse on exposes whether WithFile absolutized the path when it ran.
    ScopedEnvironmentVariable reuseInCi(L"WSLC_REUSE_IN_CI", L"1");

    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wslc-absolutize-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "first");
    std::filesystem::create_directories(root / "second");
    std::ofstream(root / "first" / "payload.txt") << "payload";
    std::ofstream(root / "second" / "payload.txt") << "payload";

    try
    {
        std::filesystem::current_path(root / "first");
        const auto relative = WslContainerBuilder{}
                                  .WithImage("alpine")
                                  .WithReuse(true)
                                  .WithFile("payload.txt", "/tmp/payload.txt")
                                  .Build();
        const auto absolute = WslContainerBuilder{}
                                  .WithImage("alpine")
                                  .WithReuse(true)
                                  .WithFile(root / "first" / "payload.txt", "/tmp/payload.txt")
                                  .Build();
        EXPECT_EQ(relative.Name(), absolute.Name());

        std::filesystem::current_path(root / "second");
        const auto other = WslContainerBuilder{}
                               .WithImage("alpine")
                               .WithReuse(true)
                               .WithFile("payload.txt", "/tmp/payload.txt")
                               .Build();
        EXPECT_NE(relative.Name(), other.Name());
    }
    catch (...)
    {
        std::filesystem::current_path(original);
        std::filesystem::remove_all(root);
        throw;
    }

    std::filesystem::current_path(original);
    std::filesystem::remove_all(root);
}

TEST(ContainerBuilder, WithVolumeRequiresAnExistingDirectory)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithVolume("missing-directory", "/data"), WslException);
}

TEST(ContainerBuilder, HostSourcesRejectReparsePoints)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wslc-reparse-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const std::filesystem::path target = directory / "target.txt";
    std::ofstream(target) << "payload";
    const std::filesystem::path link = directory / "link.txt";
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), 0) == 0)
    {
        const DWORD error = GetLastError();
        std::filesystem::remove_all(directory);
        GTEST_SKIP() << "Symbolic link creation is not available: " << error;
    }

    EXPECT_THROW(WslContainerBuilder{}.WithFile(link, "/tmp/link.txt"), WslException);
    std::filesystem::remove_all(directory);
}

TEST(ContainerBuilder, VolumeSourcesRejectReparsePoints)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wslc-reparse-volume-test";
    std::filesystem::remove_all(directory);
    const std::filesystem::path target = directory / "target";
    std::filesystem::create_directories(target);
    const std::filesystem::path link = directory / "link";
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY) == 0)
    {
        const DWORD error = GetLastError();
        std::filesystem::remove_all(directory);
        GTEST_SKIP() << "Directory symbolic link creation is not available: " << error;
    }

    EXPECT_THROW(WslContainerBuilder{}.WithVolume(link, "/data"), WslException);
    std::filesystem::remove_all(directory);
}

TEST(ContainerBuilder, ContainerGuardsAccessBeforeStart)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    EXPECT_FALSE(container.IsStarted());
    EXPECT_THROW(container.GetConnectEndpoint(8080), WslException);
    EXPECT_THROW(container.Exec("echo"), WslException);
}

TEST(ContainerBuilder, StartProcessRejectsExecOnlyOptionsBeforeContainerWork)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    wslc::ExecOptions withStandardInput;
    withStandardInput.StandardInput = "text";
    try
    {
        container.StartProcess("cat", {}, withStandardInput);
        FAIL() << "Expected WslException for StandardInput.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("StandardInput"), std::string::npos);
    }

    wslc::ExecOptions withTimeout;
    withTimeout.Timeout = 1s;
    try
    {
        container.StartProcess("cat", {}, withTimeout);
        FAIL() << "Expected WslException for Timeout.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("Timeout"), std::string::npos);
    }
}

TEST(ContainerBuilder, DisposeIsSafeForUnstartedContainers)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    container.Dispose();
    container.Dispose();
}

TEST(ContainerBuilder, WaitStrategiesAccumulate)
{
    WslContainerBuilder tooSmall;
    tooSmall.WithImage("alpine:latest")
        .WithWaitStrategy(ForWsl().WithTimeout(100s).UntilFileExists("/tmp/ready"))
        .WithWaitStrategy(ForWsl().WithTimeout(100s).UntilProcessIsRunning("nginx"))
        .WithReadinessTimeout(150s);
    EXPECT_THROW(tooSmall.Build(), WslException);

    WslContainerBuilder enough;
    enough.WithImage("alpine:latest")
        .WithWaitStrategy(ForWsl().WithTimeout(100s).UntilFileExists("/tmp/ready"))
        .WithReadinessTimeout(150s);
    EXPECT_NO_THROW(enough.Build());
}

TEST(ContainerBuilder, NetworkWaitsAcceptNonLoopbackBindAddresses)
{
    // Readiness probes honor the configured bind address, so non-loopback bindings are valid
    // with TCP and HTTP waits (no build-time rejection, no guaranteed timeout).
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine")
                        .WithPort(8080, "192.168.1.10")
                        .WithWaitStrategy(ForWsl().UntilTcpPortIsOpen(8080))
                        .WithWaitStrategy(ForWsl().UntilHttpRequestSucceeds("/health", 8080))
                        .Build());
}

TEST(ContainerBuilder, ContainerPathsRejectInjectionAcrossMethods)
{
    const std::string invalid[] = {"/proc/self/environ", "/sys/kernel", "/dev/sda", "/a/../b",
                                   "/./proc/self",       "//sys/kernel"};
    for (const std::string& path : invalid)
    {
        EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory(path), WslException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithScratchVolume("data", path, 100), WslException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithFile("missing.txt", path), WslException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithVolume("missing", path), WslException) << path;
    }

    EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory("relative"), WslException);
    EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory(std::string("/tmp/bad\x01name")), WslException);

    // Spaces are legal inside container paths even though the HTTP probe rejects them.
    EXPECT_NO_THROW(
        WslContainerBuilder{}.WithImage("alpine").WithWorkingDirectory("/mnt/c/Program Files/data").Build());
}

TEST(ContainerBuilder, CpuAndMemoryCapsAreInclusive)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine")
                        .WithCpuCount(wslc::internal::c_maxCpuCount)
                        .WithMemoryMegabytes(wslc::internal::c_maxMemoryMb)
                        .Build());
    EXPECT_THROW(WslContainerBuilder{}.WithCpuCount(wslc::internal::c_maxCpuCount + 1), WslException);
    EXPECT_THROW(WslContainerBuilder{}.WithMemoryMegabytes(wslc::internal::c_maxMemoryMb + 1), WslException);
}

TEST(ContainerBuilder, ScratchVolumeSizeCapIsInclusive)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine")
                        .WithScratchVolume("data", "/data", wslc::internal::c_maxScratchVolumeBytes)
                        .Build());

    WslContainerBuilder invalid;
    invalid.WithImage("alpine");
    EXPECT_THROW(invalid.WithScratchVolume("data", "/data", wslc::internal::c_maxScratchVolumeBytes + 1), WslException);
}

TEST(ContainerBuilder, ReadinessTimeoutCapIsInclusiveAndRejectsLongerTimeoutsBeforeStart)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithReadinessTimeout(wslc::internal::c_maxStartupTimeout).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithReadinessTimeout(wslc::internal::c_maxStartupTimeout + 1s), WslException);

    // 3650 days is past the startup budget ceiling; the builder must reject it before any
    // startup timer observes it.
    EXPECT_THROW(WslContainerBuilder{}.WithReadinessTimeout(std::chrono::hours(24 * 3650)), WslException);
}

TEST(ContainerBuilder, BuildLimitsWaitStrategies)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine").WithReadinessTimeout(wslc::internal::c_maxStartupTimeout);
    for (std::size_t i = 0; i < wslc::internal::c_maxWaitStrategies; ++i)
    {
        builder.WithWaitStrategy(ForWsl().WithTimeout(1s).UntilFileExists("/tmp/ready-" + std::to_string(i)));
    }

    EXPECT_NO_THROW(builder.Build());
    builder.WithWaitStrategy(ForWsl().WithTimeout(1s).UntilFileExists("/tmp/one-too-many"));
    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, BuildRejectsASaturatedWaitTimeoutSum)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine")
        .WithWaitStrategy(ForWsl().WithTimeout(std::chrono::milliseconds::max()).UntilFileExists("/tmp/a"))
        .WithWaitStrategy(ForWsl().WithTimeout(std::chrono::milliseconds::max()).UntilFileExists("/tmp/b"));
    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, BuildLimitsCommandArguments)
{
    std::vector<std::string> arguments(wslc::internal::c_maxCommandArguments, "arg");
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithCommand("echo", arguments).Build());

    arguments.push_back("extra");
    WslContainerBuilder tooMany;
    EXPECT_THROW(tooMany.WithImage("alpine").WithCommand("echo", arguments).Build(), WslException);
}

TEST(ContainerBuilder, WithEnvironmentEnforcesTheValueCap)
{
    const std::string atCap(wslc::internal::c_maxEnvironmentValueBytes, 'a');
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithEnvironment("BIG", atCap).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithEnvironment("BIG", atCap + "a"), WslException);
}

TEST(ContainerBuilder, BuildLimitsEnvironmentCount)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine");
    for (std::size_t i = 0; i < wslc::internal::c_maxEnvironmentVariables; ++i)
    {
        builder.WithEnvironment("VAR_" + std::to_string(i), "1");
    }

    EXPECT_NO_THROW(builder.Build());
    builder.WithEnvironment("VAR_ONE_TOO_MANY", "1");
    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, BuildLimitsFileCopiesAndVolumeMounts)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wslc-limits-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::filesystem::path source = directory / "payload.txt";
    std::ofstream(source) << "payload";

    WslContainerBuilder files;
    files.WithImage("alpine");
    for (std::size_t i = 0; i < wslc::internal::c_maxFileCopies; ++i)
    {
        files.WithFile(source, "/tmp/payload-" + std::to_string(i) + ".txt");
    }

    EXPECT_NO_THROW(files.Build());
    files.WithFile(source, "/tmp/payload-one-too-many.txt");
    EXPECT_THROW(files.Build(), WslException);

    WslContainerBuilder volumes;
    volumes.WithImage("alpine");
    for (std::size_t i = 0; i < wslc::internal::c_maxVolumeMounts; ++i)
    {
        volumes.WithVolume(directory, "/data-" + std::to_string(i));
    }

    EXPECT_NO_THROW(volumes.Build());
    volumes.WithVolume(directory, "/data-one-too-many");
    EXPECT_THROW(volumes.Build(), WslException);

    std::filesystem::remove_all(directory);
}

TEST(ContainerBuilder, ModuleBuilderRejectsAnUnboundedWaitTimeoutAtBuild)
{
    wslc::modules::PostgreSqlBuilder builder;
    builder.WithWaitTimeout(std::chrono::milliseconds::max());

    // The derived startup timeout is capped; the failure must happen while the module builds.
    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, ExecTimeoutCapIsEnforced)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    wslc::ExecOptions atCap;
    atCap.Timeout = wslc::internal::c_maxExecTimeout;
    EXPECT_THROW(container.Exec("echo", {}, atCap), WslException);

    wslc::ExecOptions overCap;
    overCap.Timeout = wslc::internal::c_maxExecTimeout + 1s;
    try
    {
        container.Exec("echo", {}, overCap);
        FAIL() << "Expected WslException for an over-cap exec timeout.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("maximum"), std::string::npos);
    }
}

TEST(ContainerBuilder, ExecRejectsMoreThanTheArgumentCap)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    const std::vector<std::string> atCap(wslc::internal::c_maxCommandArguments, "arg");
    // At the cap the arguments pass validation; the unstarted container then fails, which
    // proves the list itself was accepted.
    EXPECT_THROW(container.Exec("echo", atCap), WslException);

    const std::vector<std::string> overCap(wslc::internal::c_maxCommandArguments + 1, "arg");
    try
    {
        container.Exec("echo", overCap);
        FAIL() << "Expected WslException for an over-cap argument list.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("maximum"), std::string::npos);
    }
}

TEST(ContainerBuilder, ExecRejectsEnvironmentAboveTheCaps)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    const std::string atCapValue(wslc::internal::c_maxEnvironmentValueBytes, 'a');
    wslc::ExecOptions atCap;
    atCap.Environment["BIG"] = atCapValue;
    EXPECT_THROW(container.Exec("echo", {}, atCap), WslException);

    wslc::ExecOptions overValue;
    overValue.Environment["BIG"] = atCapValue + "a";
    try
    {
        container.Exec("echo", {}, overValue);
        FAIL() << "Expected WslException for an over-cap exec environment value.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("maximum"), std::string::npos);
    }

    wslc::ExecOptions overCount;
    for (std::size_t i = 0; i <= wslc::internal::c_maxEnvironmentVariables; ++i)
    {
        overCount.Environment["VAR_" + std::to_string(i)] = "1";
    }

    try
    {
        container.Exec("echo", {}, overCount);
        FAIL() << "Expected WslException for an over-cap exec environment.";
    }
    catch (const WslException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("maximum"), std::string::npos);
    }
}

TEST(ContainerBuilder, CompositeWaitCountCapIsEnforcedAtComposition)
{
    auto strategy = ForWsl().WithTimeout(1s).UntilFileExists("/tmp/wait-0");
    for (std::size_t i = 1; i < wslc::internal::c_maxWaitStrategies; ++i)
    {
        strategy = strategy->And(ForWsl().WithTimeout(1s).UntilFileExists("/tmp/wait-" + std::to_string(i)));
    }

    EXPECT_THROW(strategy->And(ForWsl().WithTimeout(1s).UntilFileExists("/tmp/one-too-many")), WslException);
}

TEST(ContainerBuilder, BuildLimitsScratchVolumeCount)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine");
    for (std::size_t i = 0; i < wslc::internal::c_maxScratchVolumes; ++i)
    {
        builder.WithScratchVolume("data" + std::to_string(i), "/data" + std::to_string(i), 1024);
    }

    EXPECT_NO_THROW(builder.Build());
    builder.WithScratchVolume("one-too-many", "/data-extra", 1024);
    EXPECT_THROW(builder.Build(), WslException);
}

TEST(ContainerBuilder, FromTarballRejectsATarballAboveTheSizeCap)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "wslc-tarball-cap-test.tar";
    std::filesystem::remove(path);
    std::ofstream(path).close();

    std::error_code error;
    std::filesystem::resize_file(path, wslc::internal::c_maxTarballBytes, error);
    if (error)
    {
        std::filesystem::remove(path);
        GTEST_SKIP() << "Sparse file allocation is not available: " << error.message();
    }

    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.FromTarball(path));

    std::filesystem::resize_file(path, wslc::internal::c_maxTarballBytes + 1, error);
    if (error)
    {
        std::filesystem::remove(path);
        GTEST_SKIP() << "Sparse file allocation is not available: " << error.message();
    }

    EXPECT_THROW(WslContainerBuilder{}.FromTarball(path), WslException);

    std::filesystem::remove(path);
}
