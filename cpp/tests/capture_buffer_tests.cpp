#include <gtest/gtest.h>

#include "internal/capture_buffer.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

using wslc::internal::CaptureBuffer;

namespace
{

void AppendText(CaptureBuffer& buffer, const std::string& text)
{
    buffer.Append(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::size_t CountCharacter(const std::string& text, char character)
{
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), character));
}

} // namespace

TEST(CaptureBuffer, KeepsNewestBytesAndDropsOldest)
{
    CaptureBuffer buffer;
    AppendText(buffer, std::string(700'000, 'a'));
    AppendText(buffer, std::string(700'000, 'b'));

    const std::string text = buffer.Decode();

    EXPECT_EQ(text.size(), CaptureBuffer::MaxBytes);
    EXPECT_EQ(CountCharacter(text, 'a'), 700'000 - (1'400'000 - CaptureBuffer::MaxBytes));
    EXPECT_EQ(CountCharacter(text, 'b'), 700'000u);
    EXPECT_EQ(text.substr(text.size() - 10), std::string(10, 'b'));
}

TEST(CaptureBuffer, DecodesAcrossTheRingWrapBoundary)
{
    CaptureBuffer buffer;
    AppendText(buffer, std::string(600'000, 'a'));
    AppendText(buffer, std::string(600'000, 'b'));
    AppendText(buffer, std::string(600'000, 'c'));

    const std::string text = buffer.Decode();

    EXPECT_EQ(text.size(), CaptureBuffer::MaxBytes);
    EXPECT_EQ(CountCharacter(text, 'a'), 0u);
    EXPECT_EQ(CountCharacter(text, 'b'), 448'576u);
    EXPECT_EQ(CountCharacter(text, 'c'), 600'000u);
    EXPECT_EQ(text.substr(text.size() - 10), std::string(10, 'c'));
}

TEST(CaptureBuffer, OversizedAppendKeepsOnlyItsTail)
{
    CaptureBuffer buffer;
    AppendText(buffer, std::string(CaptureBuffer::MaxBytes + 5'000, 'z'));

    const std::string text = buffer.Decode();

    EXPECT_EQ(text.size(), CaptureBuffer::MaxBytes);
    EXPECT_EQ(CountCharacter(text, 'z'), CaptureBuffer::MaxBytes);
}

TEST(CaptureBuffer, DisposedBufferIgnoresAppendsAndDecodesEmpty)
{
    CaptureBuffer buffer;
    AppendText(buffer, "before");
    buffer.Dispose();
    AppendText(buffer, "after");

    EXPECT_EQ(buffer.Decode(), std::string());
}
