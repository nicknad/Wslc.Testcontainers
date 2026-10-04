#include <gtest/gtest.h>

#include "internal/log_broadcaster.hpp"

#include <memory>
#include <string>
#include <vector>

using wslc::LogLine;
using wslc::internal::LogBroadcaster;

namespace
{

std::vector<std::string> Drain(const std::shared_ptr<LogBroadcaster::Subscriber>& subscriber)
{
    std::vector<std::string> lines;
    for (;;)
    {
        std::lock_guard lock(subscriber->Mutex);
        if (subscriber->Queue.empty())
        {
            break;
        }

        lines.push_back(std::move(subscriber->Queue.front().Text));
        subscriber->Queue.pop_front();
    }

    return lines;
}

} // namespace

TEST(LogBroadcaster, SnapshotReturnsPublishedLines)
{
    LogBroadcaster broadcaster;
    broadcaster.Publish(LogLine::Diagnostic("one"));
    broadcaster.Publish(LogLine::Diagnostic("two"));

    const auto snapshot = broadcaster.Snapshot();

    ASSERT_EQ(snapshot->size(), 2u);
    EXPECT_EQ((*snapshot)[0].Text, std::string("one"));
}

TEST(LogBroadcaster, SubscribersReceiveHistoryAndLiveLines)
{
    LogBroadcaster broadcaster;
    broadcaster.Publish(LogLine::Diagnostic("history"));

    auto subscriber = broadcaster.Subscribe();
    broadcaster.Publish(LogLine::Diagnostic("live"));
    broadcaster.Complete();

    const std::vector<std::string> lines = Drain(subscriber);

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], std::string("history"));
    EXPECT_EQ(lines[1], std::string("live"));
}

TEST(LogBroadcaster, CompletedBroadcastersReplayHistory)
{
    LogBroadcaster broadcaster;
    broadcaster.Publish(LogLine::Diagnostic("first"));
    broadcaster.Publish(LogLine::Diagnostic("second"));
    broadcaster.Complete();

    auto subscriber = broadcaster.Subscribe();
    const std::vector<std::string> lines = Drain(subscriber);

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], std::string("first"));
    EXPECT_EQ(lines[1], std::string("second"));
    EXPECT_TRUE(subscriber->Closed);
}

TEST(LogBroadcaster, HistoryIsBounded)
{
    LogBroadcaster broadcaster;
    for (int index = 0; index < 10'050; index++)
    {
        broadcaster.Publish(LogLine::Diagnostic("line " + std::to_string(index)));
    }

    const auto snapshot = broadcaster.Snapshot();

    EXPECT_EQ(snapshot->size(), LogBroadcaster::MaxHistory);
    EXPECT_EQ((*snapshot)[0].Text, std::string("line 50"));
}

TEST(LogBroadcaster, SnapshotIsCachedUntilTheNextPublish)
{
    LogBroadcaster broadcaster;
    broadcaster.Publish(LogLine::Diagnostic("one"));

    const auto first = broadcaster.Snapshot();
    EXPECT_EQ(first.get(), broadcaster.Snapshot().get());

    broadcaster.Publish(LogLine::Diagnostic("two"));
    const auto second = broadcaster.Snapshot();

    EXPECT_NE(first.get(), second.get());
    ASSERT_EQ(second->size(), 2u);
    EXPECT_EQ((*second)[1].Text, std::string("two"));
}

TEST(LogBroadcaster, SlowSubscribersDropTheOldestLinesWhenFull)
{
    LogBroadcaster broadcaster;
    auto subscriber = broadcaster.Subscribe();

    for (int index = 0; index < 1'050; index++)
    {
        broadcaster.Publish(LogLine::Diagnostic("line " + std::to_string(index)));
    }

    broadcaster.Complete();

    const std::vector<std::string> lines = Drain(subscriber);

    EXPECT_EQ(lines.size(), LogBroadcaster::MaxSubscriberBuffered);
    EXPECT_EQ(lines.front(), std::string("line 50"));
    EXPECT_EQ(lines.back(), std::string("line 1049"));
}
