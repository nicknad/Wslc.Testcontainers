#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wslc::internal
{

/// <summary>Maximum UTF-8 bytes rendered by WslReadinessException::Describe().</summary>
inline constexpr std::size_t c_maxDescribeBytes = 64u * 1024u;

/// <summary>Maximum UTF-8 bytes for one captured output section.</summary>
inline constexpr std::size_t c_maxSectionBytes = 8u * 1024u;

/// <summary>Maximum UTF-8 bytes for one captured line.</summary>
inline constexpr std::size_t c_maxLineBytes = 4u * 1024u;

/// <summary>
/// Caps a string to a UTF-8 byte budget. The result keeps the head and tail around an explicit
/// "... [N bytes omitted] ..." marker and never splits a UTF-8 sequence.
/// </summary>
std::string CapText(std::string_view value, std::size_t maxBytes);

/// <summary>
/// Caps each line to maxLineBytes (appending "...") and then the joined result to maxSectionBytes.
/// </summary>
std::string CapLines(const std::vector<std::string>& lines, std::size_t maxLineBytes, std::size_t maxSectionBytes);

} // namespace wslc::internal
