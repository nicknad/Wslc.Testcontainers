#include "wslc/resource_reaper.hpp"

#include "internal/configuration.hpp"
#include "internal/reaper_logic.hpp"
#include "internal/util.hpp"

#include <windows.h>

#include <wil/resource.h>

namespace wslc::internal
{

namespace
{

constexpr auto c_orphanGracePeriod = std::chrono::hours(24 * 7);

std::chrono::system_clock::time_point FileTimeToTimePoint(const FILETIME& value)
{
    ULARGE_INTEGER large{};
    large.LowPart = value.dwLowDateTime;
    large.HighPart = value.dwHighDateTime;
    constexpr std::uint64_t c_windowsToUnixEpoch = 116444736000000000ull;
    if (large.QuadPart <= c_windowsToUnixEpoch)
    {
        return std::chrono::system_clock::time_point{};
    }

    const auto since_epoch = large.QuadPart - c_windowsToUnixEpoch;
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(since_epoch * 100)));
}

std::chrono::system_clock::time_point DirectoryCreationTime(const std::filesystem::path& directory)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(directory.c_str(), GetFileExInfoStandard, &data) == 0)
    {
        return std::chrono::system_clock::now();
    }

    return FileTimeToTimePoint(data.ftCreationTime);
}

} // namespace

bool ShouldCleanup(const std::optional<InstanceMetadata>& metadata, bool owner_alive, bool include_reuse)
{
    if (!metadata)
    {
        // No metadata: not created by WSLC. Never delete via this path (age-gated separately).
        return false;
    }

    if (metadata->Reuse && !include_reuse)
    {
        return false;
    }

    return !owner_alive;
}

bool IsOwnerAlive(const InstanceMetadata& metadata)
{
    if (metadata.OwnerProcessId <= 0)
    {
        return false;
    }

    wil::unique_handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                           static_cast<DWORD>(metadata.OwnerProcessId)));
    if (!process)
    {
        const DWORD error = GetLastError();
        return error == ERROR_ACCESS_DENIED;
    }

    DWORD ExitCode = 0;
    if (GetExitCodeProcess(process.get(), &ExitCode) == 0 || ExitCode != STILL_ACTIVE)
    {
        return false;
    }

    // Guard PID recycling: if the current process with this PID started after the instance was
    // created, the original Owner is gone and the PID was reused. Without a recorded creation
    // time there is no safe comparison, so treat the process as alive and leave the directory to
    // the age-gated path in cleanup.
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    bool alive = true;
    if (metadata.CreatedAt && GetProcessTimes(process.get(), &creation, &exit, &kernel, &user) != 0)
    {
        const auto started = FileTimeToTimePoint(creation);
        if (started > *metadata.CreatedAt + std::chrono::minutes(1))
        {
            alive = false;
        }
    }

    return alive;
}

bool IsReuseInstanceInUse(const std::filesystem::path& instanceDirectory)
{
    const std::filesystem::path lockPath = instanceDirectory / L"wslc.lock";
    if (!std::filesystem::exists(lockPath))
    {
        return false;
    }

    const wil::unique_hfile Handle(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!Handle)
    {
        const DWORD error = GetLastError();
        // A missing file means the lock was released; anything else (sharing violation, ACL)
        // counts as in use rather than deleting blind.
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }

    return false;
}

std::vector<std::string> CleanupCore(std::stop_token token, bool include_reuse)
{
    return CleanupCore(InstanceStore::DefaultStore(), token, include_reuse);
}

std::vector<std::string> CleanupCore(InstanceStore& store, std::stop_token token, bool include_reuse)
{
    const std::filesystem::path instances = store.InstancesDirectory();
    std::error_code error;
    if (!std::filesystem::exists(instances, error))
    {
        return {};
    }

    std::vector<std::string> removed;
    for (const auto& entry : std::filesystem::directory_iterator(instances, error))
    {
        ThrowIfStopped(token);
        if (!entry.is_directory(error))
        {
            continue;
        }

        const std::string Name = ToUtf8(entry.path().filename().wstring());
        if (!WslNaming::IsManaged(Name))
        {
            continue;
        }

        const auto metadata = store.TryReadMetadata(Name);

        // Metadata that names a different instance does not vouch for this directory; it may have
        // been swapped or crafted, so never delete through it.
        if (metadata && metadata->InstanceId != Name)
        {
            continue;
        }

        if (!metadata || !metadata->CreatedAt)
        {
            // Corrupt/missing metadata or an unverifiable creation time: only delete after the
            // grace period to avoid racing a concurrent writer.
            if (std::chrono::system_clock::now() - DirectoryCreationTime(entry.path()) > c_orphanGracePeriod)
            {
                InstanceStore::BestEffortDeleteDirectory(entry.path());
                removed.push_back(Name);
            }

            continue;
        }

        const bool owner_alive = IsOwnerAlive(*metadata);
        if (!ShouldCleanup(metadata, owner_alive, include_reuse))
        {
            continue;
        }

        // A Reuse instance's metadata Owner is the process that first created it, normally dead
        // by design. The lock is the only reliable "currently in use" signal.
        if (metadata->Reuse && IsReuseInstanceInUse(entry.path()))
        {
            continue;
        }

        InstanceStore::BestEffortDeleteDirectory(entry.path());
        removed.push_back(Name);
    }

    return removed;
}

std::vector<std::string> PurgeReuseCore(std::stop_token token)
{
    return PurgeReuseCore(InstanceStore::DefaultStore(), token);
}

std::vector<std::string> PurgeReuseCore(InstanceStore& store, std::stop_token token)
{
    const std::filesystem::path instances = store.InstancesDirectory();
    std::error_code error;
    if (!std::filesystem::exists(instances, error))
    {
        return {};
    }

    std::vector<std::string> removed;
    for (const auto& entry : std::filesystem::directory_iterator(instances, error))
    {
        ThrowIfStopped(token);
        if (!entry.is_directory(error))
        {
            continue;
        }

        const std::string Name = ToUtf8(entry.path().filename().wstring());
        if (!WslNaming::IsManaged(Name))
        {
            continue;
        }

        const auto metadata = store.TryReadMetadata(Name);

        // Only delete reuse instances whose metadata names this directory.
        if (!metadata || !metadata->Reuse || metadata->InstanceId != Name)
        {
            continue;
        }

        InstanceStore::BestEffortDeleteDirectory(entry.path());
        removed.push_back(Name);
    }

    return removed;
}

} // namespace wslc::internal

namespace wslc
{

std::vector<std::string> WslResourceReaper::Cleanup(std::stop_token token)
{
    return internal::CleanupCore(token, false);
}

std::vector<std::string> WslResourceReaper::CleanupIncludingReuse(std::stop_token token)
{
    return internal::CleanupCore(token, true);
}

std::vector<std::string> WslResourceReaper::PurgeReuse(std::stop_token token)
{
    return internal::PurgeReuseCore(token);
}

} // namespace wslc
