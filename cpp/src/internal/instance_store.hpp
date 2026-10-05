#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace wslc::internal
{

/// <summary>Ownership metadata persisted Next to every WSLC-created session.</summary>
struct InstanceMetadata
{
    std::string SessionId;
    std::string InstanceId;
    int OwnerProcessId = 0;
    std::optional<std::chrono::system_clock::time_point> CreatedAt;
    std::string State = "Created";
    std::optional<std::string> Owner;
    std::optional<std::string> Image;
    bool Reuse = false;
};

/// <summary>Owns the on-disk WSLC data directory: per-instance session storage and metadata.</summary>
class InstanceStore
{
public:
    InstanceStore(std::filesystem::path DataDirectory, std::string SessionId);

    /// <summary>Shared store rooted at WslEnvironment::DataDirectory().</summary>
    static InstanceStore& DefaultStore();

    const std::string& SessionId() const noexcept { return m_sessionId; }

    std::filesystem::path InstancesDirectory() const;

    std::filesystem::path GetInstanceDirectory(const std::string& instanceName) const;

    /// <summary>
    /// Session VM storage. The runtime creates its session VHD (storage.vhdx) and its named
    /// Volumes inside this directory; metadata lives in the parent instance directory.
    /// </summary>
    std::filesystem::path GetSessionStorageDirectory(const std::string& instanceName) const;

    void WriteMetadata(const InstanceMetadata& metadata);

    std::optional<InstanceMetadata> TryReadMetadata(const std::string& instanceName) const;

    void DeleteInstanceDirectory(const std::string& instanceName);

    static std::string Sanitize(std::string_view value);

    static void BestEffortDeleteDirectory(const std::filesystem::path& path);

private:
    std::filesystem::path m_dataDirectory;
    std::string m_sessionId;
    mutable std::mutex m_metadataGate;
};

} // namespace wslc::internal
