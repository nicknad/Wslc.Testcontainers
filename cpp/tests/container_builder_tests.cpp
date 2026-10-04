#include <gtest/gtest.h>

#include <windows.h>

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"
#include "wslc/wsl_container_builder.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace std::chrono_literals;
using wslc::ContainerNetworkMode;
using wslc::VhdAllocationType;
using wslc::VolumeAccess;
using wslc::WslcException;
using wslc::WslContainerBuilder;
using wslc::waiting::ForWsl;

namespace
{

std::filesystem::path CreateTempFile(const std::string& content)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "wslc-test-input.txt";
    std::ofstream(path) << content;
    return path;
}

} // namespace

TEST(ContainerBuilder, BuildRequiresAnImageSource)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.Build(), WslcException);
}

TEST(ContainerBuilder, FromTarballRejectsAMissingTarball)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.FromTarball("does-not-exist.tar"), WslcException);
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

TEST(ContainerBuilder, WithPortValidatesAndDeduplicates)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithPort(8080).WithPort(8080).WithPort(5432).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithPort(0), WslcException);
    EXPECT_THROW(WslContainerBuilder{}.WithPort(70000), WslcException);
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
    EXPECT_THROW(builder.WithPort(8080, "not-an-ip"), WslcException);
    EXPECT_THROW(builder.WithPort(8080, ""), WslcException);

    builder.WithPort(8080, "127.0.0.1");
    EXPECT_THROW(builder.WithPort(8080, "0.0.0.0"), WslcException);
}

TEST(ContainerBuilder, WithPortNormalizesTheBindAddress)
{
    WslContainerBuilder builder;
    builder.WithImage("alpine").WithPort(8080, "0:0:0:0:0:0:0:1");

    // The IPv6 loopback spelled differently normalizes to the same address, so it must not
    // be rejected as a conflicting declaration.
    EXPECT_NO_THROW(builder.WithPort(8080, "::1").Build());
}

TEST(ContainerBuilder, WithCpuCountAndMemoryMBRecordLimits)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithCpuCount(2).WithMemoryMB(2048).Build());
    EXPECT_THROW(WslContainerBuilder{}.WithCpuCount(0), WslcException);
    EXPECT_THROW(WslContainerBuilder{}.WithMemoryMB(0), WslcException);
}

TEST(ContainerBuilder, WithNetworkingModeNoneRejectsPortsAndNetworkWaits)
{
    WslContainerBuilder ports;
    ports.WithImage("alpine").WithPort(8080).WithNetworkingMode(ContainerNetworkMode::None);
    EXPECT_THROW(ports.Build(), WslcException);

    WslContainerBuilder waits;
    waits.WithImage("alpine")
        .WithWaitStrategy(ForWsl().UntilTcpPortIsAvailable(80))
        .WithNetworkingMode(ContainerNetworkMode::None);
    EXPECT_THROW(waits.Build(), WslcException);

    WslContainerBuilder composite;
    composite.WithImage("alpine")
        .WithWaitStrategy(
            ForWsl().UntilMessageIsLogged("ready")->And(ForWsl().UntilHttpRequestIsSucceeded("/health", 8080)))
        .WithNetworkingMode(ContainerNetworkMode::None);
    EXPECT_THROW(composite.Build(), WslcException);

    // Non-network waits are fine without networking.
    WslContainerBuilder offline;
    offline.WithImage("alpine")
        .WithWaitStrategy(ForWsl().UntilFileExists("/tmp/ready"))
        .WithNetworkingMode(ContainerNetworkMode::None);
    EXPECT_NO_THROW(offline.Build());
}

TEST(ContainerBuilder, WithNetworkingModeRejectsUnknownValues)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithNetworkingMode(static_cast<ContainerNetworkMode>(99)), WslcException);
}

TEST(ContainerBuilder, WithSessionVolumeRecordsAndValidates)
{
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine").WithSessionVolume("data", "/data", 10ull * 1024 * 1024 * 1024).Build());

    WslContainerBuilder invalid;
    invalid.WithImage("alpine");
    EXPECT_THROW(invalid.WithSessionVolume("", "/data", 100), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("a/b", "/data", 100), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("a b", "/data", 100), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("data", "relative", 100), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("data", "/data", 0), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("data", "/a", 100).WithSessionVolume("data", "/b", 100), WslcException);
    EXPECT_THROW(invalid.WithSessionVolume("Data", "/a", 100).WithSessionVolume("data", "/b", 100), WslcException);

    WslContainerBuilder readOnlyFixed;
    EXPECT_NO_THROW(readOnlyFixed.WithImage("alpine")
                        .WithSessionVolume("data", "/data", 100, VolumeAccess::ReadOnly, VhdAllocationType::Fixed)
                        .Build());
}

TEST(ContainerBuilder, WithEnvironmentRejectsInvalidNames)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithEnvironment("", "value"), WslcException);
    EXPECT_THROW(builder.WithEnvironment("1INVALID", "value"), WslcException);
    EXPECT_THROW(builder.WithEnvironment("HAS-DASH", "value"), WslcException);
}

TEST(ContainerBuilder, WithFileRequiresAnExistingFile)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithFile("missing.txt", "/tmp/missing.txt"), WslcException);
}

TEST(ContainerBuilder, WithVolumeRequiresAnExistingDirectory)
{
    WslContainerBuilder builder;

    EXPECT_THROW(builder.WithVolume("missing-directory", "/data"), WslcException);
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

    EXPECT_THROW(WslContainerBuilder{}.WithFile(link, "/tmp/link.txt"), WslcException);
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

    EXPECT_THROW(WslContainerBuilder{}.WithVolume(link, "/data"), WslcException);
    std::filesystem::remove_all(directory);
}

TEST(ContainerBuilder, ContainerGuardsAccessBeforeStart)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").Build();

    EXPECT_FALSE(container.IsStarted());
    EXPECT_THROW(container.GetMappedPort(8080), WslcException);
    EXPECT_THROW(container.Exec("echo"), WslcException);
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
        .WithStartupTimeout(150s);
    EXPECT_THROW(tooSmall.Build(), WslcException);

    WslContainerBuilder enough;
    enough.WithImage("alpine:latest")
        .WithWaitStrategy(ForWsl().WithTimeout(100s).UntilFileExists("/tmp/ready"))
        .WithStartupTimeout(150s);
    EXPECT_NO_THROW(enough.Build());
}

TEST(ContainerBuilder, NetworkWaitsAcceptNonLoopbackBindAddresses)
{
    // Readiness probes honor the configured bind address, so non-loopback bindings are valid
    // with TCP and HTTP waits (no build-time rejection, no guaranteed timeout).
    WslContainerBuilder builder;
    EXPECT_NO_THROW(builder.WithImage("alpine")
                        .WithPort(8080, "192.168.1.10")
                        .WithWaitStrategy(ForWsl().UntilTcpPortIsAvailable(8080))
                        .WithWaitStrategy(ForWsl().UntilHttpRequestIsSucceeded("/health", 8080))
                        .Build());
}

TEST(ContainerBuilder, ContainerPathsRejectInjectionAcrossMethods)
{
    const std::string invalid[] = {"/proc/self/environ", "/sys/kernel", "/dev/sda", "/a/../b",
                                   "/./proc/self",       "//sys/kernel"};
    for (const std::string& path : invalid)
    {
        EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory(path), WslcException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithSessionVolume("data", path, 100), WslcException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithFile("missing.txt", path), WslcException) << path;
        EXPECT_THROW(WslContainerBuilder{}.WithVolume("missing", path), WslcException) << path;
    }

    EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory("relative"), WslcException);
    EXPECT_THROW(WslContainerBuilder{}.WithWorkingDirectory(std::string("/tmp/bad\x01name")), WslcException);

    // Spaces are legal inside container paths even though the HTTP probe rejects them.
    EXPECT_NO_THROW(
        WslContainerBuilder{}.WithImage("alpine").WithWorkingDirectory("/mnt/c/Program Files/data").Build());
}
