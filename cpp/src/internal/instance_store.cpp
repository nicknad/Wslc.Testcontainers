#include "internal/instance_store.hpp"

#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <cctype>
#include <fstream>
#include <sstream>

namespace wslc::internal
{

namespace
{

constexpr const char* c_metadataFileName = "wslc.json";

std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string RenderMetadata(const InstanceMetadata& metadata)
{
    // ordered_json preserves insertion order so wslc.json keeps its historical key layout.
    nlohmann::ordered_json document;
    document["sessionId"] = metadata.SessionId;
    document["instanceId"] = metadata.InstanceId;
    document["ownerProcessId"] = metadata.OwnerProcessId;
    if (metadata.CreatedAt)
    {
        document["createdAt"] = FormatIso8601(*metadata.CreatedAt);
    }

    document["state"] = metadata.State;
    if (metadata.Owner)
    {
        document["owner"] = *metadata.Owner;
    }

    if (metadata.Image)
    {
        document["image"] = *metadata.Image;
    }

    document["reuse"] = metadata.Reuse;
    return document.dump(2) + "\n";
}

std::optional<int> ReadInt(const nlohmann::ordered_json& value)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        try
        {
            return value.get<int>();
        }
        catch (const nlohmann::json::exception&)
        {
            return std::nullopt;
        }
    }

    if (value.is_string())
    {
        try
        {
            std::size_t consumed = 0;
            const int parsed = std::stoi(value.get_ref<const std::string&>(), &consumed);
            if (consumed == value.get_ref<const std::string&>().size())
            {
                return parsed;
            }
        }
        catch (...)
        {
        }

        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> ReadString(const nlohmann::ordered_json& value)
{
    if (!value.is_string())
    {
        return std::nullopt;
    }

    return value.get_ref<const std::string&>();
}

const nlohmann::ordered_json* FindProperty(const nlohmann::ordered_json& document, const char* key)
{
    if (!document.is_object())
    {
        return nullptr;
    }

    const auto it = document.find(key);
    return it != document.end() ? &*it : nullptr;
}

std::optional<int> ReadIntProperty(const nlohmann::ordered_json& document, const char* key)
{
    const nlohmann::ordered_json* value = FindProperty(document, key);
    return value != nullptr ? ReadInt(*value) : std::nullopt;
}

std::optional<std::string> ReadStringProperty(const nlohmann::ordered_json& document, const char* key)
{
    const nlohmann::ordered_json* value = FindProperty(document, key);
    return value != nullptr ? ReadString(*value) : std::nullopt;
}

} // namespace

InstanceStore::InstanceStore(std::filesystem::path DataDirectory, std::string SessionId)
    : m_dataDirectory(std::move(DataDirectory)), m_sessionId(std::move(SessionId))
{
}

InstanceStore& InstanceStore::DefaultStore()
{
    static InstanceStore store(WslEnvironment::DataDirectory(), WslEnvironment::SessionId());
    return store;
}

std::filesystem::path InstanceStore::InstancesDirectory() const
{
    return m_dataDirectory / L"instances";
}

std::filesystem::path InstanceStore::GetInstanceDirectory(const std::string& instanceName) const
{
    std::error_code error;
    const std::filesystem::path instances = std::filesystem::absolute(InstancesDirectory(), error).lexically_normal();
    if (error)
    {
        throw WslException("Unable to resolve the instances directory for instance '" + instanceName + "'.");
    }

    const std::string sanitizedName = Sanitize(instanceName);
    if (sanitizedName.empty())
    {
        throw WslException("Instance name '" + instanceName + "' is not a valid directory name.");
    }

    const std::filesystem::path directory =
        std::filesystem::absolute(instances / ToUtf16(sanitizedName), error).lexically_normal();
    if (error)
    {
        throw WslException("Instance name '" + instanceName + "' is not a valid directory name.");
    }

    std::wstring prefix = instances.native();
    if (!prefix.empty() && prefix.back() != L'\\' && prefix.back() != L'/')
    {
        prefix.push_back(L'\\');
    }
    const std::wstring& candidate = directory.native();
    const bool contained = candidate.size() >= prefix.size() &&
                           CompareStringOrdinal(candidate.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                                static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    if (!contained)
    {
        throw WslException("Instance name '" + instanceName + "' resolves outside the instances directory '" +
                           ToUtf8(instances.native()) + "'.");
    }

    return directory;
}

std::filesystem::path InstanceStore::GetSessionStorageDirectory(const std::string& instanceName) const
{
    return GetInstanceDirectory(instanceName) / L"storage";
}

void InstanceStore::WriteMetadata(const InstanceMetadata& metadata)
{
    const std::filesystem::path directory = GetInstanceDirectory(metadata.InstanceId);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path path = directory / ToUtf16(c_metadataFileName);
    const std::string json = RenderMetadata(metadata);

    std::lock_guard lock(m_metadataGate);

    // Write-then-rename so a crash mid-write cannot leave truncated JSON behind. A unique temp
    // Name keeps concurrent writers from clobbering each other's temp file.
    const std::filesystem::path temp = path.wstring() + L"." + ToUtf16(RandomHex(32)) + L".tmp";
    bool moved = false;
    try
    {
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(json.data(), static_cast<std::streamsize>(json.size()));
            stream.flush();
        }

        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
        {
            moved = true;
        }
    }
    catch (...)
    {
        // Best effort: a failed write must not leave temp litter behind.
    }

    if (!moved)
    {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
    }
}

std::optional<InstanceMetadata> InstanceStore::TryReadMetadata(const std::string& instanceName) const
{
    const std::filesystem::path path = GetInstanceDirectory(instanceName) / ToUtf16(c_metadataFileName);
    std::optional<std::string> Text;
    {
        std::lock_guard lock(m_metadataGate);
        Text = ReadTextFile(path);
    }

    if (!Text)
    {
        return std::nullopt;
    }

    nlohmann::ordered_json document;
    try
    {
        document = nlohmann::ordered_json::parse(Text->begin(), Text->end());
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }

    if (!document.is_object())
    {
        return std::nullopt;
    }

    InstanceMetadata metadata;
    metadata.SessionId = ReadStringProperty(document, "sessionId").value_or("");
    metadata.InstanceId = ReadStringProperty(document, "instanceId").value_or("");
    metadata.OwnerProcessId = ReadIntProperty(document, "ownerProcessId").value_or(0);
    metadata.State = ReadStringProperty(document, "state").value_or("Created");
    metadata.Owner = ReadStringProperty(document, "owner");
    metadata.Image = ReadStringProperty(document, "image");

    const auto created = ReadStringProperty(document, "createdAt");
    if (created)
    {
        const auto parsed = ParseIso8601(*created);
        if (!parsed)
        {
            return std::nullopt;
        }

        metadata.CreatedAt = *parsed;
    }

    const nlohmann::ordered_json* reuse = FindProperty(document, "reuse");
    metadata.Reuse = reuse != nullptr && reuse->is_boolean() && reuse->get<bool>();
    if (metadata.InstanceId.empty())
    {
        return std::nullopt;
    }

    return metadata;
}

void InstanceStore::DeleteInstanceDirectory(const std::string& instanceName)
{
    BestEffortDeleteDirectory(GetInstanceDirectory(instanceName));
}

std::string InstanceStore::Sanitize(std::string_view value)
{
    std::string result(value);
    for (char& character : result)
    {
        const bool allowed = std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-' ||
                             character == '_' || character == '.';
        if (!allowed)
        {
            character = '_';
        }
    }

    // Windows strips trailing dots and spaces from path components, so "a." would alias "a";
    // map them to '_' so distinct names stay distinct instead of collapsing.
    for (std::size_t index = result.size(); index > 0 && (result[index - 1] == '.' || result[index - 1] == ' ');
         index--)
    {
        result[index - 1] = '_';
    }

    return result;
}

void InstanceStore::BestEffortDeleteDirectory(const std::filesystem::path& path)
{
    try
    {
        std::error_code error;
        if (IsReparsePoint(path))
        {
            // Never recurse through a junction/reparse point: deleting the link must not touch
            // whatever directory it points at.
            std::filesystem::remove(path, error);
            return;
        }

        std::filesystem::remove_all(path, error);
    }
    catch (...)
    {
        // Best effort only.
    }
}

} // namespace wslc::internal
