#include <gtest/gtest.h>

#include "internal/instance_store.hpp"
#include "internal/util.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using wslc::internal::InstanceMetadata;
using wslc::internal::InstanceStore;

namespace
{

bool CreateDirectoryJunction(const std::filesystem::path& junction, const std::filesystem::path& target)
{
    std::wstring command = L"cmd.exe /c mklink /J \"" + junction.wstring() + L"\" \"" + target.wstring() + L"\"";
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process) == 0)
    {
        return false;
    }

    WaitForSingleObject(process.hProcess, 30'000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

class InstanceStoreTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / ("wslc-tests-" + wslc::internal::RandomHex(16));
        m_store = std::make_unique<InstanceStore>(m_root, "test-session");
    }

    void TearDown() override { InstanceStore::BestEffortDeleteDirectory(m_root); }

    InstanceMetadata CreateMetadata()
    {
        InstanceMetadata metadata;
        metadata.SessionId = m_store->SessionId();
        metadata.InstanceId = "wslc-test-1234abcd";
        metadata.OwnerProcessId = static_cast<int>(wslc::internal::CurrentProcessId());
        metadata.CreatedAt = std::chrono::system_clock::now();
        metadata.State = "Running";
        metadata.Owner = "tester";
        metadata.Image = "alpine:latest";
        metadata.Reuse = false;
        return metadata;
    }

    std::filesystem::path m_root;
    std::unique_ptr<InstanceStore> m_store;
};

} // namespace

TEST_F(InstanceStoreTest, MetadataRoundtrips)
{
    const InstanceMetadata metadata = CreateMetadata();

    m_store->WriteMetadata(metadata);
    const auto loaded = m_store->TryReadMetadata(metadata.InstanceId);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->SessionId, metadata.SessionId);
    EXPECT_EQ(loaded->InstanceId, metadata.InstanceId);
    EXPECT_EQ(loaded->OwnerProcessId, metadata.OwnerProcessId);
    EXPECT_EQ(loaded->Image.value_or(""), metadata.Image.value_or(""));
    EXPECT_EQ(loaded->Reuse, metadata.Reuse);
    EXPECT_EQ(loaded->State, metadata.State);
}

TEST_F(InstanceStoreTest, MissingMetadataReturnsNullopt)
{
    EXPECT_FALSE(m_store->TryReadMetadata("wslc-does-not-exist").has_value());
}

TEST_F(InstanceStoreTest, DeletingAnInstanceDirectoryRemovesMetadataAndStorage)
{
    const InstanceMetadata metadata = CreateMetadata();
    m_store->WriteMetadata(metadata);
    std::ofstream(m_store->GetInstanceDirectory(metadata.InstanceId) / "ext4.vhdx") << "data";

    m_store->DeleteInstanceDirectory(metadata.InstanceId);

    EXPECT_FALSE(std::filesystem::exists(m_store->GetInstanceDirectory(metadata.InstanceId)));
    EXPECT_FALSE(m_store->TryReadMetadata(metadata.InstanceId).has_value());
}

TEST_F(InstanceStoreTest, MetadataWithoutCreatedAtParsesWithEmptyCreatedAt)
{
    const std::filesystem::path directory = m_store->GetInstanceDirectory("wslc-no-created-at");
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "wslc.json")
        << "{\n  \"sessionId\": \"test-session\",\n  \"instanceId\": \"wslc-no-created-at\",\n"
           "  \"ownerProcessId\": 1234,\n  \"state\": \"Running\",\n  \"reuse\": false\n}\n";

    const auto loaded = m_store->TryReadMetadata("wslc-no-created-at");

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->InstanceId, "wslc-no-created-at");
    EXPECT_FALSE(loaded->CreatedAt.has_value());
}

TEST_F(InstanceStoreTest, DeletingAJunctionKeepsItsTarget)
{
    const std::filesystem::path target = m_root / "junction-target";
    const std::filesystem::path junction = m_root / "junction-link";
    std::filesystem::create_directories(target);
    std::ofstream(target / "marker.txt") << "data";
    if (!CreateDirectoryJunction(junction, target))
    {
        GTEST_SKIP() << "Directory junction creation is unavailable.";
    }

    ASSERT_TRUE(std::filesystem::exists(junction));

    InstanceStore::BestEffortDeleteDirectory(junction);

    EXPECT_FALSE(std::filesystem::exists(junction));
    EXPECT_TRUE(std::filesystem::exists(target / "marker.txt"));
}

TEST_F(InstanceStoreTest, InstanceDirectoryIsSanitized)
{
    const std::filesystem::path directory = m_store->GetInstanceDirectory("wslc-a/b:c");

    EXPECT_EQ(directory, m_store->InstancesDirectory() / "wslc-a_b_c");
}

TEST_F(InstanceStoreTest, ConcurrentMetadataWritesDoNotRace)
{
    const InstanceMetadata metadata = CreateMetadata();

    std::vector<std::thread> writers;
    writers.reserve(100);
    for (int index = 0; index < 100; index++)
    {
        writers.emplace_back(
            [this, &metadata, index]
            {
                InstanceMetadata updated = metadata;
                updated.State = index % 2 == 0 ? "Running" : "Stopped";
                m_store->WriteMetadata(updated);
                EXPECT_TRUE(m_store->TryReadMetadata(metadata.InstanceId).has_value());
            });
    }

    for (auto& writer : writers)
    {
        writer.join();
    }

    const std::filesystem::path instanceDirectory = m_store->GetInstanceDirectory(metadata.InstanceId);
    for (const auto& entry : std::filesystem::directory_iterator(instanceDirectory))
    {
        EXPECT_NE(entry.path().extension(), std::filesystem::path(".tmp"));
    }
}
