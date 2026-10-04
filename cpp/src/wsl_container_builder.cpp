#include "wslc/wsl_container_builder.hpp"

#include "internal/configuration.hpp"
#include "internal/limits.hpp"
#include "internal/util.hpp"
#include "internal/wait_introspection.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/platform.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <system_error>

namespace wslc
{

struct WslContainerBuilder::State
{
    internal::Configuration configuration;
};

namespace
{

constexpr std::uint64_t c_maxCopyBytes = 1024ull * 1024ull * 1024ull;

void RequireText(const std::string& value, const char* what)
{
    if (internal::IsBlank(value))
    {
        throw WslcException(std::string(what) + " must not be empty.");
    }
}

void RequireEnvironmentName(const std::string& Name)
{
    if (internal::IsBlank(Name))
    {
        throw WslcException("Environment variable name must not be empty.");
    }

    const char first = Name[0];
    if (std::isalpha(static_cast<unsigned char>(first)) == 0 && first != '_')
    {
        throw WslcException("Environment variable name '" + Name + "' must start with a letter or underscore.");
    }

    for (const char character : Name)
    {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_')
        {
            throw WslcException(std::string("Environment variable name '") + Name + "' contains invalid character '" +
                                character + "'.");
        }
    }
}

void RequireVolumeName(const std::string& Name)
{
    if (internal::IsBlank(Name))
    {
        throw WslcException("Volume Name must not be empty.");
    }

    for (const char character : Name)
    {
        if (character == '/' || character == '\\' || std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            throw WslcException("Volume Name '" + Name + "' must not contain path separators or whitespace.");
        }
    }
}

int ValidatePort(int port)
{
    if (port < 1 || port > 65535)
    {
        throw WslcException("Port must be between 1 and 65535.");
    }

    return port;
}

std::string ValidateBindAddress(const std::string& BindAddress)
{
    if (internal::IsBlank(BindAddress))
    {
        throw WslcException("Bind address must not be empty.");
    }

    const auto normalized = internal::NormalizeIpAddress(BindAddress);
    if (!normalized)
    {
        throw WslcException("Bind address '" + BindAddress + "' is not a valid IP address.");
    }

    return *normalized;
}

} // namespace

WslContainerBuilder::WslContainerBuilder() : m_state(std::make_unique<State>()) {}

WslContainerBuilder::~WslContainerBuilder() = default;

WslContainerBuilder::WslContainerBuilder(WslContainerBuilder&&) noexcept = default;

WslContainerBuilder& WslContainerBuilder::operator=(WslContainerBuilder&&) noexcept = default;

WslContainerBuilder& WslContainerBuilder::WithImage(std::string Image)
{
    RequireText(Image, "Image");
    m_state->configuration.Image = std::move(Image);
    m_state->configuration.TarballPath.reset();
    m_state->configuration.TarballImageName.reset();
    return *this;
}

WslContainerBuilder& WslContainerBuilder::FromTarball(std::filesystem::path TarballPath,
                                                      std::optional<std::string> imageName)
{
    const std::string pathText = internal::ToUtf8(TarballPath.wstring());
    RequireText(pathText, "Tarball path");
    std::error_code error;
    if (!std::filesystem::exists(TarballPath, error))
    {
        throw WslcException("Tarball '" + pathText + "' does not exist.");
    }

    const std::uintmax_t tarballLength = std::filesystem::file_size(TarballPath, error);
    if (!error)
    {
        internal::RequireTarballSize(pathText, tarballLength);
    }

    m_state->configuration.TarballPath = std::move(TarballPath);
    m_state->configuration.TarballImageName = std::move(imageName);
    m_state->configuration.Image.reset();
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithCommand(std::string command, std::vector<std::string> arguments)
{
    RequireText(command, "Command");
    m_state->configuration.Command = std::move(command);
    m_state->configuration.CommandArguments = std::move(arguments);
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithWorkingDirectory(std::string WorkingDirectory)
{
    internal::ValidateContainerPath(WorkingDirectory);
    m_state->configuration.WorkingDirectory = std::move(WorkingDirectory);
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithEnvironment(std::string Name, std::string value)
{
    RequireEnvironmentName(Name);
    internal::RequireEnvironmentValue(Name, value);
    m_state->configuration.Environment[std::move(Name)] = std::move(value);
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithEnvironmentVariables(std::map<std::string, std::string> variables)
{
    for (const auto& pair : variables)
    {
        RequireEnvironmentName(pair.first);
        internal::RequireEnvironmentValue(pair.first, pair.second);
    }

    for (auto& pair : variables)
    {
        m_state->configuration.Environment[pair.first] = std::move(pair.second);
    }

    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithPort(int port)
{
    ValidatePort(port);
    for (const auto& existing : m_state->configuration.PortMappings)
    {
        if (existing.ContainerPort == port)
        {
            if (existing.BindAddress)
            {
                throw WslcException("Port " + std::to_string(port) +
                                    " is already mapped with a different bind address. Declare each port once.");
            }

            return *this;
        }
    }

    m_state->configuration.PortMappings.push_back(internal::WslPortMappingRecord{port, std::nullopt});
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithPort(int port, std::string BindAddress)
{
    ValidatePort(port);
    const std::string normalized = ValidateBindAddress(BindAddress);
    for (const auto& existing : m_state->configuration.PortMappings)
    {
        if (existing.ContainerPort == port)
        {
            const bool same = existing.BindAddress && internal::EqualsIgnoreCase(*existing.BindAddress, normalized);
            if (!same)
            {
                throw WslcException("Port " + std::to_string(port) +
                                    " is already mapped with a different bind address. Declare each port once.");
            }

            return *this;
        }
    }

    m_state->configuration.PortMappings.push_back(internal::WslPortMappingRecord{port, normalized});
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithWaitStrategy(std::shared_ptr<waiting::IWaitStrategy> strategy)
{
    if (!strategy)
    {
        throw WslcException("The Wait strategy must not be null.");
    }

    m_state->configuration.WaitStrategies.push_back(std::move(strategy));
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithFile(std::filesystem::path HostPath, std::string ContainerPath)
{
    const std::string hostText = internal::ToUtf8(HostPath.wstring());
    RequireText(hostText, "File Source");
    internal::ValidateContainerPath(ContainerPath);
    std::error_code error;
    if (!std::filesystem::exists(HostPath, error) || std::filesystem::is_directory(HostPath, error))
    {
        throw WslcException("File Source '" + hostText + "' does not exist. Only Files are supported by WithFile.");
    }

    if (internal::IsReparsePoint(HostPath))
    {
        throw WslcException("File Source '" + hostText +
                            "' is a reparse point (symlink or junction); refusing to follow it.");
    }

    const std::uintmax_t length = std::filesystem::file_size(HostPath, error);
    if (!error && length > c_maxCopyBytes)
    {
        throw WslcException("File '" + hostText + "' exceeds 1 GiB limit (" + std::to_string(length) +
                            " bytes) and cannot be copied into the container.");
    }

    m_state->configuration.Files.push_back(
        internal::WslFileCopy{std::filesystem::absolute(HostPath), std::move(ContainerPath)});
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithVolume(std::filesystem::path HostPath, std::string ContainerPath)
{
    return WithVolume(std::move(HostPath), std::move(ContainerPath), VolumeAccess::ReadWrite);
}

WslContainerBuilder& WslContainerBuilder::WithVolume(std::filesystem::path HostPath, std::string ContainerPath,
                                                     VolumeAccess access)
{
    const std::string hostText = internal::ToUtf8(HostPath.wstring());
    RequireText(hostText, "Volume Host path");
    internal::ValidateContainerPath(ContainerPath);
    std::error_code error;
    if (!std::filesystem::exists(HostPath, error) || !std::filesystem::is_directory(HostPath, error))
    {
        throw WslcException("Volume Host path '" + hostText + "' does not exist or is not a directory.");
    }

    if (internal::IsReparsePoint(HostPath))
    {
        throw WslcException("Volume Host path '" + hostText +
                            "' is a reparse point (symlink or junction); pass the resolved directory instead.");
    }

    m_state->configuration.Volumes.push_back(internal::WslVolumeMount{
        std::filesystem::absolute(HostPath), std::move(ContainerPath), access == VolumeAccess::ReadOnly});
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithReadOnlyVolume(std::filesystem::path HostPath, std::string ContainerPath)
{
    return WithVolume(std::move(HostPath), std::move(ContainerPath), VolumeAccess::ReadOnly);
}

WslContainerBuilder& WslContainerBuilder::WithSessionVolume(std::string Name, std::string ContainerPath,
                                                            std::uint64_t SizeBytes, VolumeAccess access,
                                                            VhdAllocationType type)
{
    RequireVolumeName(Name);
    internal::ValidateContainerPath(ContainerPath);
    internal::RequireSessionVolumeSize(SizeBytes);

    for (const auto& existing : m_state->configuration.SessionVolumes)
    {
        if (internal::EqualsIgnoreCase(existing.Name, Name))
        {
            throw WslcException("A session volume '" + Name +
                                "' is already configured. Volume names must be unique per container.");
        }
    }

    m_state->configuration.SessionVolumes.push_back(internal::WslSessionVolume{
        std::move(Name), std::move(ContainerPath), access == VolumeAccess::ReadOnly, SizeBytes, type});
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithNetworkingMode(ContainerNetworkMode mode)
{
    if (mode != ContainerNetworkMode::Bridged && mode != ContainerNetworkMode::None)
    {
        throw WslcException("Unknown networking mode.");
    }

    m_state->configuration.NetworkingMode = mode;
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithCpuCount(std::uint32_t CpuCount)
{
    internal::RequireCpuCount(CpuCount);
    m_state->configuration.CpuCount = CpuCount;
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithMemoryMB(std::uint32_t megabytes)
{
    internal::RequireMemoryMb(megabytes);
    m_state->configuration.MemoryMb = megabytes;
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithReuse(bool Reuse)
{
    m_state->configuration.Reuse = Reuse;
    return *this;
}

WslContainerBuilder& WslContainerBuilder::WithStartupTimeout(std::chrono::milliseconds Timeout)
{
    internal::RequireStartupTimeout(Timeout);
    m_state->configuration.StartupTimeout = Timeout;
    return *this;
}

WslContainer WslContainerBuilder::Build()
{
    WslPlatform::ThrowIfUnsupported();
    auto configuration = m_state->configuration;

    if (!configuration.Image && !configuration.TarballPath)
    {
        if (const auto DefaultImage = WslcEnvironment::DefaultImage())
        {
            configuration.Image = *DefaultImage;
        }
    }

    if (!configuration.Image && !configuration.TarballPath)
    {
        throw WslcException(
            std::string("No Image Source configured. Call WithImage(...) or FromTarball(...), or set ") +
            WslcEnvironment::DefaultImageVariable + ".");
    }

    internal::RequireCount(configuration.CommandArguments.size(), internal::c_maxCommandArguments, "command arguments");
    internal::RequireCount(configuration.Environment.size(), internal::c_maxEnvironmentVariables,
                           "environment variables");
    internal::RequireWaitStrategyCount(configuration.WaitStrategies.size());
    internal::RequireCount(configuration.Files.size(), internal::c_maxFileCopies, "file copies");
    internal::RequireCount(configuration.Volumes.size(), internal::c_maxVolumeMounts, "volume mounts");
    internal::RequireCount(configuration.SessionVolumes.size(), internal::c_maxSessionVolumes, "session volumes");

    if (configuration.NetworkingMode == ContainerNetworkMode::None)
    {
        if (!configuration.PortMappings.empty())
        {
            throw WslcException("NetworkingMode.None provides no network: remove WithPort(...) declarations or use "
                                "Bridged networking.");
        }

        if (const auto network_wait = waiting::detail::FindNetworkWait(configuration.WaitStrategies))
        {
            throw WslcException("NetworkingMode.None provides no network: Wait strategy '" + *network_wait +
                                "' can never succeed. Remove it or use Bridged networking.");
        }
    }

    if (!configuration.WaitStrategies.empty())
    {
        std::chrono::milliseconds total{0};
        for (const auto& strategy : configuration.WaitStrategies)
        {
            if (strategy->Timeout() > std::chrono::milliseconds::max() - total)
            {
                total = std::chrono::milliseconds::max();
                break;
            }

            total += strategy->Timeout();
        }

        if (total > configuration.StartupTimeout)
        {
            throw WslcException("Startup Timeout " + internal::FormatMilliseconds(configuration.StartupTimeout) +
                                "s is smaller than the sum of Wait-strategy timeouts " +
                                internal::FormatMilliseconds(total) +
                                "s. Waits Run sequentially, so startup would always fire first. Increase "
                                "WithStartupTimeout(...) or reduce Wait WithTimeout(...) values.");
        }
    }

    return WslContainer::Create(std::move(configuration));
}

} // namespace wslc
