#include "wslc/resource_reaper.hpp"

#include "internal/configuration.hpp"
#include "internal/reaper_logic.hpp"
#include "internal/util.hpp"

#include <windows.h>

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

bool IsOwnerAlive(int process_id)
{
    if (process_id <= 0)
    {
        return false;
    }

    HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(process_id));
    if (process == nullptr)
    {
        const DWORD error = GetLastError();
        // Access denied querying another user's process: assume alive to avoid deleting live storage.
        return error == ERROR_ACCESS_DENIED;
    }

    DWORD ExitCode = 0;
    const bool alive = GetExitCodeProcess(process, &ExitCode) != 0 && ExitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

bool IsOwnerAlive(const InstanceMetadata& metadata)
{
    if (metadata.OwnerProcessId <= 0)
    {
        return false;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 static_cast<DWORD>(metadata.OwnerProcessId));
    if (process == nullptr)
    {
        const DWORD error = GetLastError();
        return error == ERROR_ACCESS_DENIED;
    }

    DWORD ExitCode = 0;
    if (GetExitCodeProcess(process, &ExitCode) == 0 || ExitCode != STILL_ACTIVE)
    {
        CloseHandle(process);
        return false;
    }

    // Guard PID recycling: if the current process with this PID started after the instance was
    // created, the original Owner is gone and the PID was reused.
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    bool alive = true;
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user) != 0)
    {
        const auto started = FileTimeToTimePoint(creation);
        if (started > metadata.CreatedAt + std::chrono::minutes(1))
        {
            alive = false;
        }
    }

    CloseHandle(process);
    return alive;
}

bool IsReuseInstanceInUse(const std::filesystem::path& instanceDirectory)
{
    const std::filesystem::path lockPath = instanceDirectory / L"wslc.lock";
    if (!std::filesystem::exists(lockPath))
    {
        return false;
    }

    const HANDLE Handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        // A missing file means the lock was released; anything else (sharing violation, ACL)
        // counts as in use rather than deleting blind.
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }

    CloseHandle(Handle);
    return false;
}

std::vector<std::string> CleanupCore(std::stop_token token, bool include_reuse)
{
    InstanceStore& store = InstanceStore::DefaultStore();
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
        if (!metadata)
        {
            // Corrupt/missing metadata: only delete after the grace period to avoid racing a
            // concurrent writer.
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
    InstanceStore& store = InstanceStore::DefaultStore();
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
        if (!metadata || !metadata->Reuse)
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
