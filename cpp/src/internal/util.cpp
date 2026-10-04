#include "internal/util.hpp"

#include "wslc/exceptions.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cctype>
#include <ctime>
#include <format>
#include <limits>

#pragma comment(lib, "bcrypt.lib")

namespace wslc::internal
{

std::wstring ToUtf16(std::string_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string ToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int size =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr,
                        nullptr);
    return result;
}

bool IsBlank(std::string_view value)
{
    for (const char character : value)
    {
        if (std::isspace(static_cast<unsigned char>(character)) == 0)
        {
            return false;
        }
    }

    return true;
}

std::string trim(std::string_view value)
{
    std::size_t Start = 0;
    std::size_t end = value.size();
    while (Start < end && std::isspace(static_cast<unsigned char>(value[Start])) != 0)
    {
        Start++;
    }

    while (end > Start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        end--;
    }

    return std::string(value.substr(Start, end - Start));
}

namespace
{

bool HasControlCharacter(std::string_view value)
{
    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F)
        {
            return true;
        }
    }

    return false;
}

/// <summary>First segment that names a real directory, skipping leading '/' runs and '.' entries.</summary>
std::string_view FirstPathSegment(std::string_view path)
{
    std::size_t index = 0;
    for (;;)
    {
        const std::size_t next = path.find('/', index);
        const std::size_t end = next == std::string_view::npos ? path.size() : next;
        const std::string_view segment = path.substr(index, end - index);
        if (!segment.empty() && segment != ".")
        {
            return segment;
        }

        if (next == std::string_view::npos)
        {
            return {};
        }

        index = next + 1;
    }
}

} // namespace

void ValidateContainerPath(std::string_view path)
{
    if (IsBlank(path))
    {
        throw WslcException("Container path must not be empty and must be an absolute Linux path (e.g. /tmp/file).");
    }

    if (path[0] != '/')
    {
        throw WslcException("Container path '" + std::string(path) +
                            "' must be an absolute Linux path starting with '/'.");
    }

    if (HasControlCharacter(path))
    {
        throw WslcException("Container path '" + std::string(path) + "' must not contain control characters.");
    }

    std::size_t index = 0;
    for (;;)
    {
        const std::size_t next = path.find('/', index);
        const std::size_t end = next == std::string_view::npos ? path.size() : next;
        if (path.substr(index, end - index) == "..")
        {
            throw WslcException("Container path '" + std::string(path) + "' must not contain '..' segments.");
        }

        if (next == std::string_view::npos)
        {
            break;
        }

        index = next + 1;
    }

    const std::string_view first = FirstPathSegment(path);
    if (first == "proc" || first == "sys" || first == "dev")
    {
        throw WslcException("Container path '" + std::string(path) + "' targets the protected '/" + std::string(first) +
                            "' filesystem.");
    }
}

void ValidateHttpPath(std::string_view value)
{
    if (IsBlank(value))
    {
        throw WslcException("HTTP wait path must not be empty.");
    }

    if (value.contains("://") || value.rfind("http:", 0) == 0 || value.rfind("https:", 0) == 0)
    {
        throw WslcException("HTTP wait path '" + std::string(value) +
                            "' must be a path-and-query (e.g. /health), not a full URL.");
    }

    if (value[0] != '/')
    {
        throw WslcException("HTTP wait path '" + std::string(value) + "' must start with '/'.");
    }

    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte <= 0x20 || byte == 0x7F)
        {
            throw WslcException("HTTP wait path '" + std::string(value) +
                                "' must not contain spaces or control characters; percent-encode them.");
        }
    }
}

std::string ToLower(std::string_view value)
{
    std::string result(value);
    for (char& character : result)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return result;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); i++)
    {
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i])))
        {
            return false;
        }
    }

    return true;
}

std::string ReadEnvironmentVariable(const char* Name)
{
    const std::wstring wideName = ToUtf16(Name);
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0)
    {
        return {};
    }

    std::wstring buffer(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), buffer.data(), size);
    if (written == 0 || written >= size)
    {
        return {};
    }

    buffer.resize(written);
    return ToUtf8(buffer);
}

std::uint32_t CurrentProcessId()
{
    return static_cast<std::uint32_t>(GetCurrentProcessId());
}

std::string ExecutableName()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            return "wslc";
        }

        if (written < buffer.size())
        {
            buffer.resize(written);
            break;
        }

        buffer.resize(buffer.size() * 2);
    }

    const std::filesystem::path path(buffer);
    const std::string Name = ToUtf8(path.stem().wstring());
    return Name.empty() ? "wslc" : Name;
}

std::string CurrentUserName()
{
    DWORD size = 0;
    GetUserNameW(nullptr, &size);
    if (size == 0)
    {
        return {};
    }

    std::wstring buffer(size, L'\0');
    if (GetUserNameW(buffer.data(), &size) == 0)
    {
        return {};
    }

    if (!buffer.empty() && buffer.back() == L'\0')
    {
        buffer.pop_back();
    }

    return ToUtf8(buffer);
}

std::string FormatWindowsError(unsigned long error)
{
    wchar_t* buffer = nullptr;
    const DWORD written =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (written == 0 || buffer == nullptr)
    {
        return "error " + std::to_string(error);
    }

    std::wstring message(buffer, written);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
    {
        message.pop_back();
    }

    return ToUtf8(message);
}

Sha256::Sha256()
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    {
        throw WslcException("Failed to open the SHA-256 provider.");
    }

    DWORD object_size = 0;
    DWORD written = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &written, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw WslcException("Failed to query the SHA-256 Object size.");
    }

    // The hash object buffer must stay alive until BCryptDestroyHash; keep it as a member.
    m_object.resize(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, m_object.data(), object_size, nullptr, 0, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw WslcException("Failed to create the SHA-256 hash.");
    }

    m_algorithm = algorithm;
    m_hash = hash;
}

Sha256::~Sha256()
{
    if (m_hash != nullptr)
    {
        BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash));
    }

    if (m_algorithm != nullptr)
    {
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_algorithm), 0);
    }
}

void Sha256::Append(std::span<const std::uint8_t> data)
{
    if (data.empty())
    {
        return;
    }

    // CNG declares the input as non-const PUCHAR but does not modify it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    if (BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(m_hash), const_cast<PUCHAR>(data.data()),
                       static_cast<ULONG>(data.size()), 0) < 0)
    {
        throw WslcException("Failed to Append SHA-256 data.");
    }
}

std::vector<std::uint8_t> Sha256::Finish()
{
    std::vector<std::uint8_t> digest(32);
    if (BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash), digest.data(), static_cast<ULONG>(digest.size()), 0) <
        0)
    {
        throw WslcException("Failed to Finish the SHA-256 hash.");
    }

    BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash));
    m_hash = nullptr;
    BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_algorithm), 0);
    m_algorithm = nullptr;
    return digest;
}

std::string ToHex(std::span<const std::uint8_t> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }

    return result;
}

std::string RandomHex(int length)
{
    if (length <= 0)
    {
        return {};
    }

    const int byte_count = (length + 1) / 2;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_count));
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    {
        throw WslcException("Failed to generate random bytes.");
    }

    return ToHex(bytes).substr(0, static_cast<std::size_t>(length));
}

std::string FormatIso8601(std::chrono::system_clock::time_point value)
{
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(value);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(value - seconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
    gmtime_s(&utc, &time);

    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                       utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(millis));
}

std::optional<std::chrono::system_clock::time_point> ParseIso8601(std::string_view value)
{
    if (value.size() < 19)
    {
        return std::nullopt;
    }

    auto digits = [&](std::size_t offset, std::size_t count, int& result) -> bool
    {
        result = 0;
        for (std::size_t i = 0; i < count; i++)
        {
            const char character = value[offset + i];
            if (character < '0' || character > '9')
            {
                return false;
            }

            result = result * 10 + (character - '0');
        }

        return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != ' ') || value[13] != ':' ||
        value[16] != ':')
    {
        return std::nullopt;
    }

    if (!digits(0, 4, year) || !digits(5, 2, month) || !digits(8, 2, day) || !digits(11, 2, hour) ||
        !digits(14, 2, minute) || !digits(17, 2, second))
    {
        return std::nullopt;
    }

    std::size_t offset = 19;
    std::int64_t fraction_ns = 0;
    if (offset < value.size() && value[offset] == '.')
    {
        offset++;
        std::int64_t scale = 100'000'000;
        while (offset < value.size() && value[offset] >= '0' && value[offset] <= '9')
        {
            if (scale > 0)
            {
                fraction_ns += static_cast<std::int64_t>(value[offset] - '0') * scale;
                scale /= 10;
            }

            offset++;
        }
    }

    int offset_minutes = 0;
    if (offset < value.size())
    {
        const char sign = value[offset];
        if (sign == 'Z' || sign == 'z')
        {
            offset++;
        }
        else if (sign == '+' || sign == '-')
        {
            offset++;
            int offset_hours = 0;
            int offset_minutes_part = 0;
            if (offset + 2 > value.size() || !digits(offset, 2, offset_hours))
            {
                return std::nullopt;
            }

            offset += 2;
            if (offset < value.size() && value[offset] == ':')
            {
                offset++;
                if (offset + 2 > value.size() || !digits(offset, 2, offset_minutes_part))
                {
                    return std::nullopt;
                }

                offset += 2;
            }

            offset_minutes = offset_hours * 60 + offset_minutes_part;
            if (sign == '-')
            {
                offset_minutes = -offset_minutes;
            }
        }
    }

    if (offset != value.size())
    {
        return std::nullopt;
    }

    std::tm utc{};
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    const std::time_t time = _mkgmtime64(&utc);
    if (time == -1)
    {
        return std::nullopt;
    }

    using namespace std::chrono;
    const auto clock_offset = seconds(-offset_minutes * 60) + nanoseconds(fraction_ns);
    return system_clock::from_time_t(time) + duration_cast<system_clock::duration>(clock_offset);
}

std::string FormatMilliseconds(std::chrono::milliseconds value)
{
    const auto total = value.count();
    const auto seconds = total / 1000;
    const auto millis = total % 1000;
    if (millis == 0)
    {
        return std::to_string(seconds);
    }

    std::string fraction = std::format("{:03}", millis < 0 ? -millis : millis);
    while (!fraction.empty() && fraction.back() == '0')
    {
        fraction.pop_back();
    }

    return std::format("{}.{}", seconds, fraction);
}

bool OrdinalLess(std::string_view left, std::string_view right)
{
    const std::wstring left_wide = ToUtf16(left);
    const std::wstring right_wide = ToUtf16(right);
    return left_wide < right_wide;
}

bool SleepFor(std::chrono::milliseconds duration, std::stop_token token)
{
    if (token.stop_requested())
    {
        return false;
    }

    if (!token.stop_possible())
    {
        std::this_thread::sleep_for(duration);
        return !token.stop_requested();
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::stop_callback callback(token, [&condition] { condition.notify_all(); });
    std::unique_lock lock(mutex);
    condition.wait_for(lock, duration, [&token] { return token.stop_requested(); });
    return !token.stop_requested();
}

void ThrowIfStopped(std::stop_token token)
{
    if (token.stop_requested())
    {
        throw OperationCanceledException();
    }
}

std::optional<bool> ParseBoolValue(std::string_view value)
{
    const std::string lowered = ToLower(trim(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
    {
        return true;
    }

    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
    {
        return false;
    }

    return std::nullopt;
}

bool IsContinuousIntegrationVariable(std::string_view name, std::string_view value)
{
    if (name == "CI" || name == "TF_BUILD" || name == "GITHUB_ACTIONS")
    {
        return ParseBoolValue(value).value_or(false);
    }

    if (name == "JENKINS_URL" || name == "TEAMCITY_VERSION")
    {
        return !IsBlank(value);
    }

    return false;
}

std::string join(const std::vector<std::string>& values, std::string_view separator)
{
    std::string result;
    for (std::size_t i = 0; i < values.size(); i++)
    {
        if (i > 0)
        {
            result += separator;
        }

        result += values[i];
    }

    return result;
}

std::optional<std::string> NormalizeIpAddress(std::string_view value)
{
    const std::string Text = trim(value);
    if (Text.empty())
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16> buffer{};
    if (InetPtonA(AF_INET, Text.c_str(), buffer.data()) == 1)
    {
        char result[INET_ADDRSTRLEN] = {};
        if (InetNtopA(AF_INET, buffer.data(), result, sizeof(result)) == nullptr)
        {
            return std::nullopt;
        }

        return std::string(result);
    }

    if (InetPtonA(AF_INET6, Text.c_str(), buffer.data()) == 1)
    {
        char result[INET6_ADDRSTRLEN] = {};
        if (InetNtopA(AF_INET6, buffer.data(), result, sizeof(result)) == nullptr)
        {
            return std::nullopt;
        }

        return std::string(result);
    }

    return std::nullopt;
}

bool IsWildcardAddress(std::string_view value)
{
    const auto normalized = NormalizeIpAddress(value);
    return normalized && (*normalized == "0.0.0.0" || *normalized == "::");
}

void EnsureReplaceableDestination(const std::filesystem::path& destination)
{
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    const std::string text = ToUtf8(destination.wstring());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        throw WslProcessException("Refusing to write '" + text +
                                  "': the destination is a reparse point (symlink or junction).");
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        throw WslProcessException("Refusing to write '" + text + "': the destination is a directory.");
    }
}

std::filesystem::path CreateAdjacentTempFile(const std::filesystem::path& destination)
{
    const std::filesystem::path parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path(L".");
    wchar_t buffer[MAX_PATH] = {};
    if (GetTempFileNameW(parent.c_str(), L"wsl", 0, buffer) == 0)
    {
        throw WslProcessException("Failed to create a temporary file next to '" + ToUtf8(destination.wstring()) +
                                  "': " + FormatWindowsError(GetLastError()));
    }

    return std::filesystem::path(buffer);
}

void CommitFileReplace(const std::filesystem::path& temp, const std::filesystem::path& destination)
{
    if (MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        throw WslProcessException("Failed to replace '" + ToUtf8(destination.wstring()) +
                                  "': " + FormatWindowsError(GetLastError()));
    }
}

void BestEffortDeleteFile(const std::filesystem::path& path)
{
    DeleteFileW(path.c_str());
}

bool IsReparsePoint(const std::filesystem::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

} // namespace wslc::internal
