#pragma once

#include "wslc/waiting/i_wait_strategy.hpp"
#include "wslc/wsl_container.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wslc
{

/// <summary>Networking mode of the WSLC session.</summary>
enum class ContainerNetworkMode
{
    /// <summary>Bridged networking with a NIC (default). Ports and network waits apply.</summary>
    Bridged = 0,

    /// <summary>
    /// No NIC: the container is fully isolated. Cannot be combined with ports or network waits.
    /// Detection covers only the built-in TCP/HTTP Wait Strategies; a custom IWaitStrategy that
    /// needs the network bypasses this validation.
    /// </summary>
    Isolated = 1,
};

/// <summary>Access mode for a Windows directory mounted into the container.</summary>
enum class VolumeAccess
{
    /// <summary>Read-write mount (default).</summary>
    ReadWrite = 0,

    /// <summary>Read-only mount.</summary>
    ReadOnly = 1,
};

/// <summary>VHD allocation strategy for a scratch volume.</summary>
enum class VhdAllocationType
{
    /// <summary>Grows on demand (default).</summary>
    Dynamic = 0,

    /// <summary>Pre-allocates the full size.</summary>
    Fixed = 1,
};

/// <summary>
/// Mutable Testcontainers-style builder for <see cref="WslContainer"/> instances.
/// Every <c>with...</c>/<c>from...</c> call mutates this builder and returns it for chaining.
/// <c>Build()</c> snapshots the current configuration, so later builder changes do not affect
/// containers already built. Builders are not thread-safe.
/// </summary>
class WslContainerBuilder final
{
public:
    WslContainerBuilder();
    ~WslContainerBuilder();
    WslContainerBuilder(WslContainerBuilder&&) noexcept;
    WslContainerBuilder& operator=(WslContainerBuilder&&) noexcept;

    /// <summary>
    /// Uses a container Image. The Image is pulled on first use. The Image's ENTRYPOINT/CMD is
    /// not executed automatically; declare the service with <c>WithCommand</c> or use a module
    /// builder, otherwise only a keep-alive shell runs.
    /// </summary>
    WslContainerBuilder& WithImage(std::string image);

    /// <summary>
    /// Imports a root filesystem tarball as a container Image. The tarball must exist when this
    /// is called and must not exceed 1 TiB. <paramref Name="imageName"/> defaults to a
    /// WSLC-generated local Name.
    /// </summary>
    WslContainerBuilder& FromTarball(std::filesystem::path TarballPath,
                                     std::optional<std::string> imageName = std::nullopt);

    /// <summary>Sets the long-running command started as the container init process. At most 1000 arguments may be
    /// configured.</summary>
    WslContainerBuilder& WithCommand(std::string command, std::vector<std::string> arguments = {});

    /// <summary>Sets the working directory used by the init process and command executions.</summary>
    WslContainerBuilder& WithWorkingDirectory(std::string WorkingDirectory);

    /// <summary>Adds an Environment variable scoped to the container processes. The value must not exceed 128 KiB; at
    /// most 1000 variables may be configured.</summary>
    WslContainerBuilder& WithEnvironment(std::string name, std::string value);

    /// <summary>Adds Environment variables scoped to the container processes. At most 1000 variables with values up to
    /// 128 KiB each may be configured.</summary>
    WslContainerBuilder& WithEnvironmentVariables(std::map<std::string, std::string> variables);

    /// <summary>
    /// Declares a Linux TCP service port exposed on a dynamic Windows port. UDP mappings are not
    /// supported: the WSLC runtime returns E_NOTIMPL for them.
    /// </summary>
    WslContainerBuilder& WithPort(int port);

    /// <summary>
    /// Declares a Linux TCP service port bound to a specific Windows address (e.g. 0.0.0.0 to
    /// expose it on the LAN). The Windows port stays dynamic.
    /// </summary>
    WslContainerBuilder& WithPort(int port, std::string BindAddress);

    /// <summary>Adds a readiness strategy. All configured Strategies must pass before startup completes. At most 16
    /// strategies may be configured.</summary>
    WslContainerBuilder& WithWaitStrategy(std::shared_ptr<waiting::IWaitStrategy> strategy);

    /// <summary>
    /// Copies a Windows file into the container during startup. Files larger than 1 GiB are
    /// rejected; at most 64 files may be configured. The container path must be an absolute
    /// Linux Destination.
    /// </summary>
    WslContainerBuilder& WithFile(std::filesystem::path HostPath, std::string ContainerPath);

    /// <summary>Mounts a Windows directory into the container as read-write (Host, container order). At most 64 mounts
    /// may be configured.</summary>
    WslContainerBuilder& WithVolume(std::filesystem::path HostPath, std::string ContainerPath);

    /// <summary>Mounts a Windows directory into the container with an explicit access mode.</summary>
    WslContainerBuilder& WithVolume(std::filesystem::path HostPath, std::string ContainerPath, VolumeAccess access);

    /// <summary>
    /// Mounts a scratch VHD volume (native Linux ext4) into the container. The volume is
    /// recreated empty on every Start: it is size-limited scratch space, not persistence.
    /// <paramref Name="SizeBytes"/> must be positive and at most 1 TiB.
    /// </summary>
    WslContainerBuilder& WithScratchVolume(std::string Name, std::string ContainerPath, std::uint64_t SizeBytes,
                                           VolumeAccess access = VolumeAccess::ReadWrite,
                                           VhdAllocationType type = VhdAllocationType::Dynamic);

    /// <summary>
    /// Sets the container networking mode. Isolated fully isolates the container (no NIC): no
    /// <c>WithPort</c> and no network Wait Strategies may be combined with it. Note that
    /// detection covers only the built-in TCP/HTTP Wait Strategies; a custom IWaitStrategy that
    /// needs the network bypasses this validation.
    /// </summary>
    WslContainerBuilder& WithNetworkingMode(ContainerNetworkMode mode);

    /// <summary>Caps the session CPU count. Must be between 1 and 64.</summary>
    WslContainerBuilder& WithCpuCount(std::uint32_t CpuCount);

    /// <summary>Caps the session memory in megabytes. Must be between 1 and 1048576 MB (1 TiB).</summary>
    WslContainerBuilder& WithMemoryMegabytes(std::uint32_t megabytes);

    /// <summary>
    /// Enables Reuse across test runs. The instance Name is derived from the configuration hash.
    /// Reuse is still disabled under CI unless WSLC_REUSE_IN_CI is truthy. The session VHD
    /// (including the pulled Image cache) is kept between runs; scratch Volumes are still
    /// recreated empty on every Start. Reusable instances are never auto-deleted.
    /// </summary>
    WslContainerBuilder& WithReuse(bool Reuse = true);

    /// <summary>
    /// Overrides the readiness Timeout that bounds the whole startup. Must be greater than or
    /// equal to the sum of configured Wait-strategy timeouts (waits Run sequentially) and at
    /// most 24 hours; Build() fails otherwise.
    /// </summary>
    WslContainerBuilder& WithReadinessTimeout(std::chrono::milliseconds timeout);

    /// <summary>Validates the configuration and creates the container. The container is not started.</summary>
    WslContainer Build();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace wslc
