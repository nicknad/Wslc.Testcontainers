#pragma once

#include "wslc/waiting/i_wait_strategy.hpp"
#include "wslc/wsl_container_builder.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wslc::internal
{

/// <summary>A Linux TCP port exposed on a dynamic Windows port.</summary>
struct WslPortMappingRecord
{
    int ContainerPort = 0;
    std::optional<std::string> BindAddress;
};

/// <summary>A Host file copied into the container.</summary>
struct WslFileCopy
{
    std::filesystem::path Source;
    std::string Destination;
};

/// <summary>A Windows directory mounted into the container.</summary>
struct WslVolumeMount
{
    std::filesystem::path HostPath;
    std::string ContainerPath;
    bool ReadOnly = false;
};

/// <summary>A session VHD volume mounted into the container (recreated empty on every Start).</summary>
struct WslSessionVolume
{
    std::string Name;
    std::string ContainerPath;
    bool ReadOnly = false;
    std::uint64_t SizeBytes = 0;
    VhdAllocationType type = VhdAllocationType::Dynamic;
};

/// <summary>Immutable description of the container requested through the builder.</summary>
struct Configuration
{
    std::optional<std::string> Image;
    std::optional<std::filesystem::path> TarballPath;
    std::optional<std::string> TarballImageName;
    std::optional<std::string> Command;
    std::vector<std::string> CommandArguments;
    std::optional<std::string> WorkingDirectory;
    std::map<std::string, std::string> Environment;
    std::vector<WslPortMappingRecord> PortMappings;
    std::vector<std::shared_ptr<waiting::IWaitStrategy>> WaitStrategies;
    std::vector<WslFileCopy> Files;
    std::vector<WslVolumeMount> Volumes;
    std::vector<WslSessionVolume> SessionVolumes;
    std::optional<ContainerNetworkMode> NetworkingMode;
    std::optional<std::uint32_t> CpuCount;
    std::optional<std::uint32_t> MemoryMb;
    std::optional<bool> Reuse;
    std::chrono::milliseconds StartupTimeout = std::chrono::seconds(120);
};

/// <summary>Generates collision-free session names and recognizes WSLC-owned resources.</summary>
class WslNaming
{
public:
    static constexpr const char* Prefix = "wslc-";

    static std::string CreateInstanceName(const std::string& SessionId);
    static std::string CreateReuseName(const std::string& configHash);
    static bool IsManaged(std::string_view Name);
    static std::string Slug(std::string_view value, std::size_t maxLength = 24);
};

/// <summary>Computes a stable identity for reusable environments.</summary>
class WslConfigHasher
{
public:
    /// <summary>
    /// Identity covers configuration metadata (paths and settings), not WithFile Source contents:
    /// those are copied into the container on every Start, so hashing them would put file I/O
    /// behind the public Name.
    /// </summary>
    static std::string Compute(const Configuration& configuration);
};

} // namespace wslc::internal
