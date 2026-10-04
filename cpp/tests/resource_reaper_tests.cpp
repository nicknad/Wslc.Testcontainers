#include <gtest/gtest.h>

#include "internal/instance_store.hpp"
#include "internal/reaper_logic.hpp"
#include "internal/util.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>

using wslc::internal::InstanceMetadata;
using wslc::internal::InstanceStore;

namespace
{

InstanceMetadata CreateMetadata(bool reuse)
{
    InstanceMetadata metadata;
    metadata.SessionId = "session";
    metadata.InstanceId = "wslc-test-0000";
    metadata.OwnerProcessId = static_cast<int>(wslc::internal::CurrentProcessId());
    metadata.CreatedAt = std::chrono::system_clock::now();
    metadata.Reuse = reuse;
    return metadata;
}

} // namespace

TEST(ResourceReaper, UnknownMetadataIsNeverCleaned)
{
    EXPECT_FALSE(wslc::internal::ShouldCleanup(std::nullopt, false, false));
    EXPECT_FALSE(wslc::internal::ShouldCleanup(std::nullopt, true, false));
}

TEST(ResourceReaper, EphemeralInstancesAreCleanedWhenTheOwnerIsGone)
{
    const std::optional<InstanceMetadata> metadata = CreateMetadata(false);

    EXPECT_FALSE(wslc::internal::ShouldCleanup(metadata, true, false));
    EXPECT_TRUE(wslc::internal::ShouldCleanup(metadata, false, false));
}

TEST(ResourceReaper, ReusableInstancesArePreserved)
{
    const std::optional<InstanceMetadata> metadata = CreateMetadata(true);

    EXPECT_FALSE(wslc::internal::ShouldCleanup(metadata, true, false));
    EXPECT_FALSE(wslc::internal::ShouldCleanup(metadata, false, false));
    EXPECT_TRUE(wslc::internal::ShouldCleanup(metadata, false, true));
}

TEST(ResourceReaper, OwnerLivenessIsDetected)
{
    EXPECT_TRUE(wslc::internal::IsOwnerAlive(static_cast<int>(wslc::internal::CurrentProcessId())));
    EXPECT_FALSE(wslc::internal::IsOwnerAlive(0));
    EXPECT_FALSE(wslc::internal::IsOwnerAlive(INT_MAX));
}

TEST(ResourceReaper, ReuseInstanceLockIsDetected)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("wslc-tests-" + wslc::internal::RandomHex(16));
    std::filesystem::create_directories(directory);
    try
    {
        EXPECT_FALSE(wslc::internal::IsReuseInstanceInUse(directory));

        const std::filesystem::path lockPath = directory / L"wslc.lock";
        const HANDLE handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(handle, INVALID_HANDLE_VALUE);
        EXPECT_TRUE(wslc::internal::IsReuseInstanceInUse(directory));
        CloseHandle(handle);

        EXPECT_FALSE(wslc::internal::IsReuseInstanceInUse(directory));
    }
    catch (...)
    {
        InstanceStore::BestEffortDeleteDirectory(directory);
        throw;
    }

    InstanceStore::BestEffortDeleteDirectory(directory);
}
