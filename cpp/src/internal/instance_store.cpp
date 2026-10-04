#include "internal/instance_store.hpp"

#include "internal/json.hpp"
#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"

#include <windows.h>

#include <cctype>
#include <format>
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
    std::string json =
        std::format("{{\n  \"sessionId\": \"{}\",\n  \"instanceId\": \"{}\",\n  \"ownerProcessId\": {}",
                    json::Escape(metadata.SessionId), json::Escape(metadata.InstanceId), metadata.OwnerProcessId);
    if (metadata.CreatedAt)
    {
        json += std::format(",\n  \"createdAt\": \"{}\"", FormatIso8601(*metadata.CreatedAt));
    }

    json += std::format(",\n  \"state\": \"{}\"", json::Escape(metadata.State));
    if (metadata.Owner)
    {
        json += std::format(",\n  \"owner\": \"{}\"", json::Escape(*metadata.Owner));
    }

    if (metadata.Image)
    {
        json += std::format(",\n  \"image\": \"{}\"", json::Escape(*metadata.Image));
    }

    json += std::format(",\n  \"reuse\": {}\n}}\n", metadata.Reuse ? "true" : "false");
    return json;
}

std::optional<int> ReadInt(const json::Value* value)
{
    if (value == nullptr)
    {
        return std::nullopt;
    }

    if (value->IsNumber())
    {
        return static_cast<int>(value->Number);
    }

    if (value->IsString())
    {
        try
        {
            return std::stoi(value->String);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::string> ReadString(const json::Value* value)
{
    if (value == nullptr || !value->IsString())
    {
        return std::nullopt;
    }

    return value->String;
}

} // namespace

InstanceStore::InstanceStore(std::filesystem::path DataDirectory, std::string SessionId)
    : m_dataDirectory(std::move(DataDirectory)), m_sessionId(std::move(SessionId))
{
}

InstanceStore& InstanceStore::DefaultStore()
{
    static InstanceStore store(WslcEnvironment::DataDirectory(), WslcEnvironment::SessionId());
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
        throw WslcException("Unable to resolve the instances directory for instance '" + instanceName + "'.");
    }

    const std::string sanitizedName = Sanitize(instanceName);
    if (sanitizedName.empty())
    {
        throw WslcException("Instance name '" + instanceName + "' is not a valid directory name.");
    }

    const std::filesystem::path directory =
        std::filesystem::absolute(instances / ToUtf16(sanitizedName), error).lexically_normal();
    if (error)
    {
        throw WslcException("Instance name '" + instanceName + "' is not a valid directory name.");
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
        throw WslcException("Instance name '" + instanceName + "' resolves outside the instances directory '" +
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

    const auto document = json::Parse(*Text);
    if (!document || !document->IsObject())
    {
        return std::nullopt;
    }

    InstanceMetadata metadata;
    metadata.SessionId = ReadString(document->Find("sessionId")).value_or("");
    metadata.InstanceId = ReadString(document->Find("instanceId")).value_or("");
    metadata.OwnerProcessId = ReadInt(document->Find("ownerProcessId")).value_or(0);
    metadata.State = ReadString(document->Find("state")).value_or("Created");
    metadata.Owner = ReadString(document->Find("owner"));
    metadata.Image = ReadString(document->Find("image"));

    const auto created = ReadString(document->Find("createdAt"));
    if (created)
    {
        const auto parsed = ParseIso8601(*created);
        if (!parsed)
        {
            return std::nullopt;
        }

        metadata.CreatedAt = *parsed;
    }

    const json::Value* Reuse = document->Find("reuse");
    metadata.Reuse = Reuse != nullptr && Reuse->IsBoolean() && Reuse->Boolean;
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
