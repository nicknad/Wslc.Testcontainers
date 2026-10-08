#include <gtest/gtest.h>

#include "internal/instance_store.hpp"
#include "internal/reaper_logic.hpp"
#include "internal/util.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

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

void SetDirectoryCreationTime(const std::filesystem::path& directory, std::chrono::system_clock::time_point value)
{
    const HANDLE handle = CreateFileW(directory.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);

    constexpr std::uint64_t c_windowsToUnixEpoch = 116444736000000000ull;
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count();
    ULARGE_INTEGER large{};
    large.QuadPart = static_cast<std::uint64_t>(sinceEpoch / 100) + c_windowsToUnixEpoch;
    const FILETIME creation{large.LowPart, large.HighPart};

    EXPECT_NE(SetFileTime(handle, &creation, nullptr, nullptr), 0);
    CloseHandle(handle);
}

class ResourceReaperTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / ("wslc-tests-" + wslc::internal::RandomHex(16));
        m_store = std::make_unique<InstanceStore>(m_root, "test-session");
    }

    void TearDown() override { InstanceStore::BestEffortDeleteDirectory(m_root); }

    std::filesystem::path m_root;
    std::unique_ptr<InstanceStore> m_store;
};

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
    // Test IsOwnerAlive via InstanceMetadata to avoid exposing IsOwnerAlive(int)
    InstanceMetadata metadata;
    metadata.SessionId = "test-session";
    metadata.InstanceId = "wslc-owner-liveness";
    metadata.CreatedAt = std::chrono::system_clock::now();

    metadata.OwnerProcessId = static_cast<int>(wslc::internal::CurrentProcessId());
    EXPECT_TRUE(wslc::internal::IsOwnerAlive(metadata));

    metadata.OwnerProcessId = 0;
    EXPECT_FALSE(wslc::internal::IsOwnerAlive(metadata));

    metadata.OwnerProcessId = INT_MAX;
    EXPECT_FALSE(wslc::internal::IsOwnerAlive(metadata));
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

TEST_F(ResourceReaperTest, OwnerLivenessWithoutCreatedAtDoesNotAssumeProcessReuse)
{
    InstanceMetadata metadata = CreateMetadata(false);
    metadata.CreatedAt = std::nullopt;

    // Before the fix the missing timestamp defaulted to the epoch, so the running owner process
    // always looked like a reused PID.
    EXPECT_TRUE(wslc::internal::IsOwnerAlive(metadata));
}

TEST_F(ResourceReaperTest, CleanupGraceGatesMetadataWithoutCreatedAt)
{
    InstanceMetadata metadata = CreateMetadata(false);
    metadata.InstanceId = "wslc-created-at-missing";
    metadata.OwnerProcessId = INT_MAX;
    metadata.CreatedAt = std::nullopt;
    m_store->WriteMetadata(metadata);

    const std::filesystem::path directory = m_store->GetInstanceDirectory(metadata.InstanceId);
    ASSERT_TRUE(std::filesystem::exists(directory));

    EXPECT_TRUE(wslc::internal::CleanupCore(*m_store, std::stop_token{}, false).empty());
    EXPECT_TRUE(std::filesystem::exists(directory));

    SetDirectoryCreationTime(directory, std::chrono::system_clock::now() - std::chrono::hours(24 * 8));

    const auto removed = wslc::internal::CleanupCore(*m_store, std::stop_token{}, false);
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed.front(), metadata.InstanceId);
    EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST_F(ResourceReaperTest, CleanupSkipsMetadataForAnotherInstance)
{
    const std::string name = "wslc-identity-mismatch";
    const std::filesystem::path directory = m_store->GetInstanceDirectory(name);
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "wslc.json")
        << "{\n  \"sessionId\": \"test-session\",\n  \"instanceId\": \"wslc-some-other-instance\",\n"
           "  \"ownerProcessId\": 2147483647,\n  \"createdAt\": \"2020-01-01T00:00:00.000Z\",\n"
           "  \"state\": \"Running\",\n  \"reuse\": false\n}\n";
    SetDirectoryCreationTime(directory, std::chrono::system_clock::now() - std::chrono::hours(24 * 30));

    EXPECT_TRUE(wslc::internal::CleanupCore(*m_store, std::stop_token{}, false).empty());
    EXPECT_TRUE(std::filesystem::exists(directory));
}

TEST_F(ResourceReaperTest, PurgeReuseSkipsMetadataForAnotherInstance)
{
    const std::string name = "wslc-reuse-identity";
    const std::filesystem::path directory = m_store->GetInstanceDirectory(name);
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "wslc.json")
        << "{\n  \"sessionId\": \"test-session\",\n  \"instanceId\": \"wslc-some-other-instance\",\n"
           "  \"ownerProcessId\": 2147483647,\n  \"createdAt\": \"2020-01-01T00:00:00.000Z\",\n"
           "  \"state\": \"Running\",\n  \"reuse\": true\n}\n";

    EXPECT_TRUE(wslc::internal::PurgeReuseCore(*m_store, std::stop_token{}).empty());
    EXPECT_TRUE(std::filesystem::exists(directory));
}
