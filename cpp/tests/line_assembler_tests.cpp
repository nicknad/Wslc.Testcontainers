#include <gtest/gtest.h>

#include "internal/line_assembler.hpp"

#include <atomic>
#include <cstdint>
#include <format>
#include <latch>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

using wslc::internal::LineAssembler;

namespace
{

void AppendText(LineAssembler& assembler, const std::string& text)
{
    assembler.Append(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::string Checksum(const std::string& text)
{
    std::uint32_t hash = 17;
    for (const unsigned char ch : text)
    {
        hash = (hash * 31u) + ch;
    }

    return std::format("{:08x}", hash);
}

std::string MakeLine(int writer, int index)
{
    const std::string body = std::format("w{}-{}", writer, index);
    return std::format("{}-{}", body, Checksum(body));
}

} // namespace

TEST(LineAssembler, SplitsLinesAndTrimsCarriageReturns)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });

    AppendText(assembler, "one\r\ntwo\n");

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], std::string("one"));
    EXPECT_EQ(lines[1], std::string("two"));
}

TEST(LineAssembler, KeepsPartialLineUntilFlush)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });

    AppendText(assembler, "partial");
    EXPECT_TRUE(lines.empty());

    assembler.Flush();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], std::string("partial"));
}

TEST(LineAssembler, ReassemblesMultibyteCharactersSplitAcrossChunks)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });
    const std::string bytes = "h\xC3\xA9llo\n";

    // The first chunk ends in the middle of the two-byte 'e-acute' sequence.
    AppendText(assembler, bytes.substr(0, 2));
    AppendText(assembler, bytes.substr(2));

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], std::string("h\xC3\xA9llo"));
}

TEST(LineAssembler, EmitsEveryLineFromASingleLargeChunk)
{
    constexpr int lineCount = 10'000;
    std::string payload;
    for (int i = 0; i < lineCount; i++)
    {
        payload += std::to_string(i);
        payload += '\n';
    }

    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });
    AppendText(assembler, payload);

    ASSERT_EQ(lines.size(), static_cast<std::size_t>(lineCount));
    EXPECT_EQ(lines.front(), std::string("0"));
    EXPECT_EQ(lines.back(), std::string("9999"));
}

TEST(LineAssembler, TrimsCarriageReturnSplitAcrossChunks)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });

    AppendText(assembler, "one\r");
    AppendText(assembler, "\ntwo");

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], std::string("one"));

    assembler.Flush();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[1], std::string("two"));
}

TEST(LineAssembler, SplitsASingleLineThatExceedsThePendingCap)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });
    const std::string payload(LineAssembler::MaxPendingBytes + 1, 'x');

    AppendText(assembler, payload);
    assembler.Flush();

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], payload);
}

TEST(LineAssembler, ScansALongLineOnceAcrossManyChunks)
{
    std::vector<std::string> lines;
    LineAssembler assembler([&lines](std::string line) { lines.push_back(std::move(line)); });

    for (int i = 0; i < 1'000; i++)
    {
        AppendText(assembler, "chunk");
    }

    AppendText(assembler, "\nnext\n");

    std::string expected;
    for (int i = 0; i < 1'000; i++)
    {
        expected += "chunk";
    }

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], expected);
    EXPECT_EQ(lines[1], std::string("next"));
}

TEST(LineAssembler, ConcurrentAppendsAndFlushChurnPreserveEveryLine)
{
    constexpr int writerCount = 4;
    constexpr int linesPerWriter = 2'000;
    std::vector<std::string> published;
    std::mutex publishedMutex;
    LineAssembler assembler(
        [&published, &publishedMutex](std::string line)
        {
            std::lock_guard lock(publishedMutex);
            published.push_back(std::move(line));
        });

    std::atomic<int> failures{0};
    std::latch start(writerCount + 2);
    std::jthread flusher(
        [&assembler, &failures, &start](std::stop_token token)
        {
            try
            {
                start.arrive_and_wait();
                while (!token.stop_requested())
                {
                    assembler.Flush();
                }
            }
            catch (...)
            {
                failures.fetch_add(1);
            }
        });

    std::vector<std::jthread> writers;
    writers.reserve(writerCount);
    for (int writer = 0; writer < writerCount; ++writer)
    {
        writers.emplace_back(
            [&assembler, &failures, &start, writer]
            {
                try
                {
                    start.arrive_and_wait();
                    for (int index = 0; index < linesPerWriter; ++index)
                    {
                        const std::string line = MakeLine(writer, index);
                        const std::string payload = index % 3 == 0 ? line + "\r\n" : line + "\n";
                        AppendText(assembler, payload);
                    }
                }
                catch (...)
                {
                    failures.fetch_add(1);
                }
            });
    }

    start.arrive_and_wait();
    writers.clear();
    flusher.request_stop();
    flusher.join();
    assembler.Flush();

    EXPECT_EQ(failures.load(), 0);

    std::unordered_set<std::string> expected;
    for (int writer = 0; writer < writerCount; ++writer)
    {
        for (int index = 0; index < linesPerWriter; ++index)
        {
            expected.insert(MakeLine(writer, index));
        }
    }

    std::unordered_set<std::string> seen;
    {
        std::lock_guard lock(publishedMutex);
        for (const std::string& line : published)
        {
            EXPECT_TRUE(seen.insert(line).second) << "duplicate line: " << line;
        }
    }

    EXPECT_EQ(published.size(), expected.size());
    EXPECT_TRUE(seen == expected);
}

TEST(LineAssembler, FragmentedAppendsWithConcurrentFlushesPreserveEveryLine)
{
    constexpr int lineCount = 2'000;
    std::vector<std::string> published;
    std::mutex publishedMutex;
    LineAssembler assembler(
        [&published, &publishedMutex](std::string line)
        {
            std::lock_guard lock(publishedMutex);
            published.push_back(std::move(line));
        });

    std::atomic<int> failures{0};
    std::latch start(4);
    std::vector<std::string> expected;
    std::mutex lineMutex;

    // Two flushers call Flush concurrently with the fragmenting writer. The gate keeps whole
    // logical lines from interleaving: callbacks are published outside the assembler's state
    // lock, so cross-caller publication order is not part of the contract. Flush racing with
    // a non-empty pending buffer is covered by ConcurrentFlushesPublishPendingDataExactlyOnce.
    const auto flushLoop = [&assembler, &failures, &start, &lineMutex](std::stop_token token)
    {
        try
        {
            start.arrive_and_wait();
            while (!token.stop_requested())
            {
                std::lock_guard lock(lineMutex);
                assembler.Flush();
            }
        }
        catch (...)
        {
            failures.fetch_add(1);
        }
    };

    std::jthread firstFlusher(flushLoop);
    std::jthread secondFlusher(flushLoop);
    std::jthread writer(
        [&assembler, &failures, &start, &expected, &lineMutex]
        {
            try
            {
                start.arrive_and_wait();
                for (int index = 0; index < lineCount; ++index)
                {
                    const std::string line = MakeLine(0, index);
                    {
                        std::lock_guard lock(lineMutex);

                        // The line arrives in fragments with no newline until the last one.
                        const std::size_t first = line.size() / 3;
                        AppendText(assembler, line.substr(0, first));
                        AppendText(assembler, line.substr(first, first));
                        AppendText(assembler, line.substr(2 * first));
                        AppendText(assembler, "\n");
                    }

                    expected.push_back(line);
                }
            }
            catch (...)
            {
                failures.fetch_add(1);
            }
        });

    start.arrive_and_wait();
    writer.join();
    firstFlusher.request_stop();
    secondFlusher.request_stop();
    firstFlusher.join();
    secondFlusher.join();
    assembler.Flush();

    EXPECT_EQ(failures.load(), 0);

    std::lock_guard lock(publishedMutex);
    EXPECT_EQ(published, expected);
}

TEST(LineAssembler, ConcurrentFlushesPublishPendingDataExactlyOnce)
{
    constexpr int iterations = 200;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        std::vector<std::string> published;
        std::mutex publishedMutex;
        LineAssembler assembler(
            [&published, &publishedMutex](std::string line)
            {
                std::lock_guard lock(publishedMutex);
                published.push_back(std::move(line));
            });

        const std::string pending = "partial-" + std::to_string(iteration);
        AppendText(assembler, pending);

        std::latch gate(3);
        const auto flushOnce = [&assembler, &gate]
        {
            gate.arrive_and_wait();
            assembler.Flush();
        };

        std::jthread first(flushOnce);
        std::jthread second(flushOnce);
        gate.arrive_and_wait();
        first.join();
        second.join();

        ASSERT_EQ(published.size(), 1u) << "iteration " << iteration;
        EXPECT_EQ(published[0], pending) << "iteration " << iteration;
    }
}

TEST(LineAssembler, ReentrantAppendFromCallbackDoesNotDeadlock)
{
    std::vector<std::string> lines;
    LineAssembler* assembler = nullptr;
    bool reentered = false;
    LineAssembler instance(
        [&lines, &assembler, &reentered](std::string line)
        {
            if (line == "third" && !reentered)
            {
                reentered = true;
                AppendText(*assembler, "second\n");
            }

            lines.push_back(std::move(line));
        });
    assembler = &instance;

    AppendText(instance, "first\nthird\n");
    instance.Flush();

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], std::string("first"));
    EXPECT_EQ(lines[1], std::string("second"));
    EXPECT_EQ(lines[2], std::string("third"));
}
