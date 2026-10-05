#include "internal/text_truncation.hpp"

#include <algorithm>
#include <format>

namespace wslc::internal
{

namespace
{

bool IsContinuationByte(char value)
{
    return (static_cast<unsigned char>(value) & 0xC0u) == 0x80u;
}

std::size_t Utf8HeadLength(std::string_view value, std::size_t length)
{
    while (length > 0 && length < value.size() && IsContinuationByte(value[length]))
    {
        --length;
    }

    return length;
}

std::size_t Utf8TailStart(std::string_view value, std::size_t start)
{
    while (start < value.size() && IsContinuationByte(value[start]))
    {
        ++start;
    }

    return start;
}

std::string CapLine(std::string_view line, std::size_t maxBytes)
{
    if (line.size() <= maxBytes)
    {
        return std::string(line);
    }

    constexpr std::string_view suffix = "...";
    if (maxBytes <= suffix.size())
    {
        return std::string(suffix.substr(0, maxBytes));
    }

    const std::size_t headLength = Utf8HeadLength(line, maxBytes - suffix.size());
    return std::string(line.substr(0, headLength)) + std::string(suffix);
}

} // namespace

std::string CapText(std::string_view value, std::size_t maxBytes)
{
    if (value.size() <= maxBytes)
    {
        return std::string(value);
    }

    if (maxBytes == 0)
    {
        return {};
    }

    std::size_t headLength = maxBytes / 2;
    std::size_t tailStart = value.size() - (maxBytes - headLength);
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        headLength = Utf8HeadLength(value, std::min(headLength, value.size()));
        tailStart = Utf8TailStart(value, std::clamp(tailStart, headLength, value.size()));

        const std::size_t omitted = value.size() - headLength - (value.size() - tailStart);
        const std::string marker = std::format("... [{} bytes omitted] ...", omitted);
        const std::size_t used = headLength + marker.size() + (value.size() - tailStart);
        if (used <= maxBytes)
        {
            return std::string(value.substr(0, headLength)) + marker + std::string(value.substr(tailStart));
        }

        const std::size_t overflow = used - maxBytes;
        const std::size_t tailLength = value.size() - tailStart;
        if (overflow <= headLength)
        {
            headLength -= overflow;
        }
        else if (overflow - headLength <= tailLength)
        {
            tailStart += overflow - headLength;
            headLength = 0;
        }
        else
        {
            break;
        }
    }

    return std::string(value.substr(0, Utf8HeadLength(value, maxBytes)));
}

std::string CapLines(const std::vector<std::string>& lines, std::size_t maxLineBytes, std::size_t maxSectionBytes)
{
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
        {
            joined.push_back('\n');
        }

        joined += CapLine(lines[i], maxLineBytes);
    }

    return CapText(joined, maxSectionBytes);
}

} // namespace wslc::internal
