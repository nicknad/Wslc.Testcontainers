#include <gtest/gtest.h>

#include "internal/process_registry.hpp"

#include <cstddef>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

using wslc::internal::ContainerProcessState;
using wslc::internal::ProcessRegistry;

namespace
{

std::shared_ptr<ContainerProcessState> CreateState()
{
    return std::make_shared<ContainerProcessState>(false, nullptr);
}

} // namespace

TEST(ProcessRegistry, AddsAndSnapshotsProcesses)
{
    ProcessRegistry registry;
    auto first = CreateState();
    auto second = CreateState();

    registry.Add(first);
    registry.Add(second);

    const auto snapshot = registry.Snapshot();
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].get(), first.get());
    EXPECT_EQ(snapshot[1].get(), second.get());
}

TEST(ProcessRegistry, RemovingAProcessTakesItOutOfTheSnapshot)
{
    ProcessRegistry registry;
    auto process = CreateState();
    registry.Add(process);

    registry.Remove(process);

    EXPECT_TRUE(registry.Snapshot().empty());
}

TEST(ProcessRegistry, SnapshotIsACopy)
{
    ProcessRegistry registry;
    auto state = CreateState();
    registry.Add(state);

    const auto snapshot = registry.Snapshot();
    registry.Clear();

    EXPECT_EQ(snapshot.size(), 1u);
    EXPECT_TRUE(registry.Snapshot().empty());
}

TEST(ProcessRegistry, PrunesExpiredEntriesOnAdd)
{
    ProcessRegistry registry;
    auto expired = CreateState();
    registry.Add(expired);
    expired.reset();

    auto live = CreateState();
    registry.Add(live);

    const auto snapshot = registry.Snapshot();
    ASSERT_EQ(snapshot.size(), 1u);
    EXPECT_EQ(snapshot[0].get(), live.get());
}

TEST(ProcessRegistry, TakeAllReturnsLiveProcessesAndEmptiesTheRegistry)
{
    ProcessRegistry registry;
    auto first = CreateState();
    auto second = CreateState();

    registry.Add(first);
    registry.Add(second);

    const auto taken = registry.TakeAll();
    ASSERT_EQ(taken.size(), 2u);
    EXPECT_EQ(taken[0].get(), first.get());
    EXPECT_EQ(taken[1].get(), second.get());
    EXPECT_TRUE(registry.Snapshot().empty());

    auto expired = CreateState();
    registry.Add(expired);
    expired.reset();
    EXPECT_TRUE(registry.TakeAll().empty());
}

TEST(ProcessRegistry, TakeAllKeepsProcessesAddedAfterward)
{
    ProcessRegistry registry;
    auto first = CreateState();
    auto second = CreateState();

    registry.Add(first);
    const auto firstTaken = registry.TakeAll();
    ASSERT_EQ(firstTaken.size(), 1u);
    EXPECT_EQ(firstTaken[0].get(), first.get());

    registry.Add(second);

    const auto secondTaken = registry.TakeAll();
    ASSERT_EQ(secondTaken.size(), 1u);
    EXPECT_EQ(secondTaken[0].get(), second.get());
    EXPECT_TRUE(registry.Snapshot().empty());
}

TEST(ProcessRegistry, TakeAllDoesNotDropProcessesAddedConcurrently)
{
    constexpr int processCount = 2'000;
    ProcessRegistry registry;
    std::vector<std::shared_ptr<ContainerProcessState>> added;
    added.reserve(static_cast<std::size_t>(processCount) + 1);
    auto sentinel = CreateState();

    std::thread producer(
        [&]
        {
            for (int i = 0; i < processCount; ++i)
            {
                auto process = CreateState();
                added.push_back(process);
                registry.Add(process);
            }

            added.push_back(sentinel);
            registry.Add(sentinel);
        });

    std::unordered_set<const ContainerProcessState*> drained;
    bool sawSentinel = false;
    while (!sawSentinel)
    {
        for (const auto& process : registry.TakeAll())
        {
            EXPECT_TRUE(drained.insert(process.get()).second);
            sawSentinel = process.get() == sentinel.get();
        }
    }

    producer.join();

    EXPECT_EQ(added.size(), static_cast<std::size_t>(processCount) + 1);
    EXPECT_EQ(drained.size(), added.size());
    for (const auto& process : added)
    {
        EXPECT_EQ(drained.count(process.get()), 1u);
    }

    EXPECT_TRUE(registry.TakeAll().empty());
}
