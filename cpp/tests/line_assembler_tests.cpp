#include <gtest/gtest.h>

#include "internal/line_assembler.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using wslc::internal::LineAssembler;

namespace
{

void AppendText(LineAssembler& assembler, const std::string& text)
{
    assembler.Append(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
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
