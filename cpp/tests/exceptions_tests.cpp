#include <gtest/gtest.h>

#include "internal/readiness_diagnostics.hpp"
#include "internal/text_truncation.hpp"
#include "wslc/exceptions.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using wslc::LogLine;
using wslc::LogSource;
using wslc::WslReadinessException;

namespace
{

constexpr std::size_t c_lineBytes = 256u * 1024u;
constexpr std::size_t c_lineCount = 50;
constexpr char c_secret[] = "SECRET_BEYOND_THE_CAP";

std::string MakeHugeStream()
{
    std::string result;
    result.reserve(c_lineCount * (c_lineBytes + 1));
    for (std::size_t i = 0; i < c_lineCount; ++i)
    {
        const char filler = static_cast<char>('a' + (i % 26));
        if (i == c_lineCount / 2)
        {
            result.append(c_lineBytes / 2, filler);
            result.append(c_secret);
            result.append(c_lineBytes / 2 - (sizeof(c_secret) - 1), filler);
        }
        else
        {
            result.append(c_lineBytes, filler);
        }

        result.push_back('\n');
    }

    return result;
}

bool IsValidUtf8(std::string_view value)
{
    std::size_t index = 0;
    while (index < value.size())
    {
        const unsigned char current = static_cast<unsigned char>(value[index]);
        std::size_t continuation = 0;
        if (current < 0x80u)
        {
            continuation = 0;
        }
        else if ((current & 0xE0u) == 0xC0u)
        {
            continuation = 1;
        }
        else if ((current & 0xF0u) == 0xE0u)
        {
            continuation = 2;
        }
        else if ((current & 0xF8u) == 0xF0u)
        {
            continuation = 3;
        }
        else
        {
            return false;
        }

        if (index + continuation >= value.size())
        {
            return false;
        }

        for (std::size_t offset = 1; offset <= continuation; ++offset)
        {
            if ((static_cast<unsigned char>(value[index + offset]) & 0xC0u) != 0x80u)
            {
                return false;
            }
        }

        index += continuation + 1;
    }

    return true;
}

std::vector<LogLine> MakeHugeLogs()
{
    std::vector<LogLine> logs;
    logs.reserve(c_lineCount);
    for (std::size_t i = 0; i < c_lineCount; ++i)
    {
        const char filler = static_cast<char>('a' + (i % 26));
        std::string text(c_lineBytes, filler);
        if (i == c_lineCount / 2)
        {
            text.replace(c_lineBytes / 2, sizeof(c_secret) - 1, c_secret);
        }

        logs.push_back(LogLine{LogSource::Stdout, std::move(text), {}});
    }

    return logs;
}

} // namespace

TEST(Exceptions, DescribeBoundsCapturedOutput)
{
    const auto enriched = wslc::internal::ReadinessDiagnostics::Enrich(
        WslReadinessException("Timed out waiting for readiness", "TCP port 5432", 5s, MakeHugeLogs()), "alpine:latest",
        "sleep infinity", 1, MakeHugeStream(), MakeHugeStream());

    const std::string described = enriched.Describe();

    EXPECT_LE(described.size(), wslc::internal::c_maxDescribeBytes);
    EXPECT_NE(described.find("WSLC readiness failed"), std::string::npos);
    EXPECT_NE(described.find("Image:        alpine:latest"), std::string::npos);
    EXPECT_NE(described.find("Command:      sleep infinity"), std::string::npos);
    EXPECT_NE(described.find("Expected:     TCP port 5432"), std::string::npos);
    EXPECT_NE(described.find("Timeout:      5s"), std::string::npos);
    EXPECT_NE(described.find("Exit code:    1"), std::string::npos);
    EXPECT_NE(described.find("Last stdout:"), std::string::npos);
    EXPECT_NE(described.find("Last stderr:"), std::string::npos);
    EXPECT_NE(described.find("Recent Logs:"), std::string::npos);
    EXPECT_NE(described.find("bytes omitted"), std::string::npos);
    EXPECT_EQ(described.find(c_secret), std::string::npos);
}

TEST(Exceptions, DescribeIsUnchangedForSmallDiagnostics)
{
    const std::string logText = "00:00:00.000 [stderr] oops";
    const auto enriched = wslc::internal::ReadinessDiagnostics::Enrich(
        WslReadinessException("Timed out waiting for readiness", "TCP port 5432", 5s,
                              std::vector<LogLine>{LogLine{LogSource::Stderr, "oops", {}}}),
        "alpine:latest", "sleep infinity", 3, "hello", "bad");

    std::string expected = "WSLC readiness failed\n"
                           "\n"
                           "Image:        alpine:latest\n"
                           "Command:      sleep infinity\n"
                           "Expected:     TCP port 5432\n"
                           "Timeout:      5s\n"
                           "Exit code:    3\n"
                           "\n"
                           "Last stdout:\n"
                           "hello\n"
                           "\n"
                           "Last stderr:\n"
                           "bad\n"
                           "\n"
                           "Recent Logs:\n";
    expected += logText + "\n";

    EXPECT_EQ(enriched.Describe(), expected);
}

TEST(Exceptions, DescribeKeepsTheNoCommandHint)
{
    const WslReadinessException exception("timed out", "TCP port 5432", 5s, {});
    const std::string described = exception.Describe();

    EXPECT_NE(described.find("Command:      <none>"), std::string::npos);
    EXPECT_NE(described.find("Hint:"), std::string::npos);
    EXPECT_NE(described.find("ENTRYPOINT/CMD"), std::string::npos);
    EXPECT_LE(described.size(), wslc::internal::c_maxDescribeBytes);
}

TEST(Exceptions, CapTextKeepsHeadAndTailAroundTheMarker)
{
    const std::string value = std::string(1024, 'a') + "middle" + std::string(1024, 'b');
    const std::string capped = wslc::internal::CapText(value, 512);

    EXPECT_LE(capped.size(), 512u);
    EXPECT_NE(capped.find("bytes omitted"), std::string::npos);
    EXPECT_EQ(capped.substr(0, 16), std::string(16, 'a'));
    EXPECT_EQ(capped.substr(capped.size() - 16), std::string(16, 'b'));
    EXPECT_EQ(wslc::internal::CapText("short", 16), "short");
    EXPECT_EQ(wslc::internal::CapText("abcdef", 0), "");
    EXPECT_EQ(wslc::internal::CapText("abcdef", 2), "ab");
}

TEST(Exceptions, CapTextNeverSplitsUtf8Sequences)
{
    std::string value;
    for (int i = 0; i < 64; ++i)
    {
        value += "\xC3\xA9";
    }

    const std::string capped = wslc::internal::CapText(value, 31);

    EXPECT_LE(capped.size(), 31u);
    EXPECT_TRUE(IsValidUtf8(capped));
}

TEST(Exceptions, CapLinesCapsEachLine)
{
    const std::string capped = wslc::internal::CapLines({std::string(8192, 'x')}, 4096, 8192);

    EXPECT_EQ(capped.size(), 4096u);
    EXPECT_EQ(capped.substr(capped.size() - 3), "...");
}
