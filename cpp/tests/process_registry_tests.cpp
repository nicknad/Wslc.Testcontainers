#include <gtest/gtest.h>

#include "internal/process_registry.hpp"

#include <memory>

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
