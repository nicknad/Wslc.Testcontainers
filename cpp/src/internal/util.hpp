#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace wslc::internal
{

/// <summary>Converts a UTF-8 string to UTF-16.</summary>
std::wstring ToUtf16(std::string_view value);

/// <summary>Converts a UTF-16 string to UTF-8.</summary>
std::string ToUtf8(std::wstring_view value);

/// <summary>Returns true when the string is empty or contains only whitespace.</summary>
bool IsBlank(std::string_view value);

/// <summary>Trims leading/trailing whitespace.</summary>
std::string trim(std::string_view value);

/// <summary>Maximum size of a single exec standard input payload.</summary>
inline constexpr std::size_t c_maxStandardInputBytes = 64u * 1024u * 1024u;

/// <summary>Throws WslProcessException when standard input exceeds the 64 MiB limit.</summary>
void ValidateStandardInputSize(std::size_t byteCount);

/// <summary>
/// Requires an absolute Linux path that is safe to pass to the WSLC runtime as a working
/// directory, volume target, or copy destination. Throws WslcException otherwise.
/// </summary>
void ValidateContainerPath(std::string_view path);

/// <summary>
/// Requires an origin-form path-and-query for the raw HTTP probe. Throws WslcException when
/// the value is a full URL, is relative, or contains spaces/control characters.
/// </summary>
void ValidateHttpPath(std::string_view value);

/// <summary>Lowercases ASCII characters.</summary>
std::string ToLower(std::string_view value);

/// <summary>Case-insensitive ASCII equality.</summary>
bool EqualsIgnoreCase(std::string_view left, std::string_view right);

/// <summary>Reads an Environment variable; empty string when unset.</summary>
std::string ReadEnvironmentVariable(const char* Name);

/// <summary>Current process Id.</summary>
std::uint32_t CurrentProcessId();

/// <summary>Executable file Name without extension (falls back to "wslc").</summary>
std::string ExecutableName();

/// <summary>Current user Name (falls back to an empty string).</summary>
std::string CurrentUserName();

/// <summary>Formats a Windows error code for diagnostics.</summary>
std::string FormatWindowsError(unsigned long error);

/// <summary>Incremental SHA-256 hasher backed by CNG.</summary>
class Sha256
{
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void Append(std::span<const std::uint8_t> data);
    std::vector<std::uint8_t> Finish();

private:
    void* m_algorithm = nullptr;
    void* m_hash = nullptr;
    std::vector<std::uint8_t> m_object;
};

/// <summary>Returns the lowercase hex encoding of a byte sequence.</summary>
std::string ToHex(std::span<const std::uint8_t> bytes);

/// <summary>Returns a random lowercase hex string of the requested length.</summary>
std::string RandomHex(int length);

/// <summary>Formats a UTC Timestamp as an ISO-8601 string with millisecond precision.</summary>
std::string FormatIso8601(std::chrono::system_clock::time_point value);

/// <summary>Parses ISO-8601 timestamps with 'Z' or +/-HH:MM offsets and optional fractions.</summary>
std::optional<std::chrono::system_clock::time_point> ParseIso8601(std::string_view value);

/// <summary>Formats milliseconds with up to three decimals (invariant, e.g. "1.5").</summary>
std::string FormatMilliseconds(std::chrono::milliseconds value);

/// <summary>Ordinal (UTF-16 code unit) less-than, matching .NET string.CompareOrdinal.</summary>
bool OrdinalLess(std::string_view left, std::string_view right);

/// <summary>Sleeps for the duration; returns false when the Stop token fired first.</summary>
bool SleepFor(std::chrono::milliseconds duration, std::stop_token token);

/// <summary>Throws OperationCanceledException when the token has fired.</summary>
void ThrowIfStopped(std::stop_token token);

/// <summary>Parses a Boolean-ish Environment value (1/true/yes/on, 0/false/no/off).</summary>
std::optional<bool> ParseBoolValue(std::string_view value);

/// <summary>
/// Classifies an Environment variable as a continuous-integration signal: Boolean-parsed names
/// are truthy flags; presence-checked names signal CI whenever non-blank.
/// </summary>
bool IsContinuousIntegrationVariable(std::string_view name, std::string_view value);

/// <summary>Joins strings with a separator.</summary>
std::string join(const std::vector<std::string>& values, std::string_view separator);

/// <summary>Parses and canonicalizes an IPv4/IPv6 literal; nullopt when invalid.</summary>
std::optional<std::string> NormalizeIpAddress(std::string_view value);

/// <summary>Returns true for the IPv4/IPv6 unspecified (wildcard) addresses.</summary>
bool IsWildcardAddress(std::string_view value);

/// <summary>Throws when a copy destination is a directory or a reparse point (symlink/junction).</summary>
void EnsureReplaceableDestination(const std::filesystem::path& destination);

/// <summary>Creates a unique empty temp file next to the destination and returns its path.</summary>
std::filesystem::path CreateAdjacentTempFile(const std::filesystem::path& destination);

/// <summary>Atomically replaces the destination with the finished temp file.</summary>
void CommitFileReplace(const std::filesystem::path& temp, const std::filesystem::path& destination);

/// <summary>Deletes a file, ignoring failures.</summary>
void BestEffortDeleteFile(const std::filesystem::path& path);

/// <summary>Returns true when the path exists and is a reparse point (symlink/junction).</summary>
bool IsReparsePoint(const std::filesystem::path& path);

} // namespace wslc::internal
