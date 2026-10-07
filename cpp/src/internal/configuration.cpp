#include "internal/configuration.hpp"

#include "internal/util.hpp"

#include <algorithm>
#include <cctype>

namespace wslc::internal
{

namespace
{

class HashWriter
{
public:
    void WriteString(const std::optional<std::string>& value)
    {
        if (!value)
        {
            WriteInt32(-1);
            return;
        }

        WriteInt32(static_cast<std::int32_t>(value->size()));
        if (!value->empty())
        {
            m_hasher.Append(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value->data()), value->size()));
        }
    }

    void WriteInt32(std::int32_t value)
    {
        const std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(value & 0xFF),
            static_cast<std::uint8_t>((value >> 8) & 0xFF),
            static_cast<std::uint8_t>((value >> 16) & 0xFF),
            static_cast<std::uint8_t>((value >> 24) & 0xFF),
        };
        m_hasher.Append(bytes);
    }

    void WriteUInt32(std::optional<std::uint32_t> value)
    {
        if (!value)
        {
            WriteInt32(-1);
            return;
        }

        const std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(*value & 0xFF),
            static_cast<std::uint8_t>((*value >> 8) & 0xFF),
            static_cast<std::uint8_t>((*value >> 16) & 0xFF),
            static_cast<std::uint8_t>((*value >> 24) & 0xFF),
        };
        m_hasher.Append(bytes);
    }

    void WriteUInt64(std::uint64_t value)
    {
        std::uint8_t bytes[8];
        for (int i = 0; i < 8; i++)
        {
            bytes[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
        m_hasher.Append(bytes);
    }

    void WriteByte(std::uint8_t value) { m_hasher.Append(std::span<const std::uint8_t>(&value, 1)); }

    std::vector<std::uint8_t> Finish() { return m_hasher.Finish(); }

private:
    Sha256 m_hasher;
};

} // namespace

std::string WslNaming::CreateInstanceName(const std::string& SessionId)
{
    return std::string(Prefix) + Slug(SessionId) + "-" + RandomHex(8);
}

std::string WslNaming::CreateReuseName(const std::string& configHash)
{
    return std::string(Prefix) + "reuse-" + configHash.substr(0, 12);
}

bool WslNaming::IsManaged(std::string_view Name)
{
    constexpr std::string_view managedPrefix = "wslc-";
    if (Name.size() < managedPrefix.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < managedPrefix.size(); i++)
    {
        const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(Name[i])));
        if (left != managedPrefix[i])
        {
            return false;
        }
    }

    return true;
}

std::string WslNaming::Slug(std::string_view value, std::size_t maxLength)
{
    std::string builder;
    builder.reserve(std::min(value.size(), maxLength + 8));
    for (const char character : value)
    {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0)
        {
            builder.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        else if (!builder.empty() && builder.back() != '-')
        {
            builder.push_back('-');
        }
    }

    while (!builder.empty() && builder.back() == '-')
    {
        builder.pop_back();
    }

    std::size_t Start = 0;
    while (Start < builder.size() && builder[Start] == '-')
    {
        Start++;
    }

    builder.erase(0, Start);
    if (builder.empty())
    {
        builder = "session";
    }

    if (builder.size() > maxLength)
    {
        builder.resize(maxLength);
    }

    return builder;
}

std::string WslConfigHasher::Compute(const Configuration& configuration)
{
    HashWriter writer;
    writer.WriteString(configuration.Image);
    writer.WriteString(configuration.TarballPath
                           ? std::optional<std::string>(ToUtf8(configuration.TarballPath->wstring()))
                           : std::nullopt);
    writer.WriteString(configuration.TarballImageName);
    writer.WriteString(configuration.Command);

    writer.WriteInt32(static_cast<std::int32_t>(configuration.CommandArguments.size()));
    for (const auto& argument : configuration.CommandArguments)
    {
        writer.WriteString(argument);
    }

    writer.WriteString(configuration.WorkingDirectory);

    std::vector<std::pair<std::string, std::string>> Environment(configuration.Environment.begin(),
                                                                 configuration.Environment.end());
    std::sort(Environment.begin(), Environment.end(),
              [](const auto& left, const auto& right) { return OrdinalLess(left.first, right.first); });
    writer.WriteInt32(static_cast<std::int32_t>(Environment.size()));
    for (const auto& pair : Environment)
    {
        writer.WriteString(pair.first);
        writer.WriteString(pair.second);
    }

    std::vector<WslPortMappingRecord> ports = configuration.PortMappings;
    std::sort(ports.begin(), ports.end(),
              [](const auto& left, const auto& right)
              {
                  if (left.ContainerPort != right.ContainerPort)
                  {
                      return left.ContainerPort < right.ContainerPort;
                  }

                  const std::string left_bind = left.BindAddress.value_or("");
                  const std::string right_bind = right.BindAddress.value_or("");
                  return OrdinalLess(left_bind, right_bind);
              });
    writer.WriteInt32(static_cast<std::int32_t>(ports.size()));
    for (const auto& port : ports)
    {
        writer.WriteInt32(port.ContainerPort);
        writer.WriteString(port.BindAddress);
    }

    writer.WriteInt32(configuration.NetworkingMode ? static_cast<std::int32_t>(*configuration.NetworkingMode) : -1);
    writer.WriteUInt32(configuration.CpuCount);
    writer.WriteUInt32(configuration.MemoryMb);

    std::vector<WslFileCopy> Files = configuration.Files;
    std::sort(Files.begin(), Files.end(),
              [](const auto& left, const auto& right)
              {
                  if (left.Destination != right.Destination)
                  {
                      return OrdinalLess(left.Destination, right.Destination);
                  }

                  return OrdinalLess(ToUtf8(left.Source.wstring()), ToUtf8(right.Source.wstring()));
              });
    writer.WriteInt32(static_cast<std::int32_t>(Files.size()));
    for (const auto& file : Files)
    {
        writer.WriteString(ToUtf8(file.Source.wstring()));
        writer.WriteString(file.Destination);
    }

    std::vector<WslVolumeMount> Volumes = configuration.Volumes;
    std::sort(Volumes.begin(), Volumes.end(),
              [](const auto& left, const auto& right)
              {
                  if (left.ContainerPath != right.ContainerPath)
                  {
                      return OrdinalLess(left.ContainerPath, right.ContainerPath);
                  }

                  return OrdinalLess(ToUtf8(left.HostPath.wstring()), ToUtf8(right.HostPath.wstring()));
              });
    writer.WriteInt32(static_cast<std::int32_t>(Volumes.size()));
    for (const auto& volume : Volumes)
    {
        writer.WriteString(ToUtf8(volume.HostPath.wstring()));
        writer.WriteString(volume.ContainerPath);
        writer.WriteByte(volume.ReadOnly ? 1 : 0);
    }

    std::vector<WslScratchVolume> ScratchVolumes = configuration.ScratchVolumes;
    std::sort(ScratchVolumes.begin(), ScratchVolumes.end(),
              [](const auto& left, const auto& right)
              {
                  if (left.ContainerPath != right.ContainerPath)
                  {
                      return OrdinalLess(left.ContainerPath, right.ContainerPath);
                  }

                  return OrdinalLess(left.Name, right.Name);
              });
    writer.WriteInt32(static_cast<std::int32_t>(ScratchVolumes.size()));
    for (const auto& volume : ScratchVolumes)
    {
        writer.WriteString(volume.Name);
        writer.WriteString(volume.ContainerPath);
        writer.WriteByte(volume.ReadOnly ? 1 : 0);
        writer.WriteUInt64(volume.SizeBytes);
        writer.WriteInt32(static_cast<std::int32_t>(volume.type));
    }

    return ToHex(writer.Finish());
}

} // namespace wslc::internal
