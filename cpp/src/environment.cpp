#include "wslc/environment.hpp"

#include "internal/util.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace wslc
{

namespace
{

std::optional<std::chrono::milliseconds> ParseClockTime(std::string_view value)
{
    const std::string trimmed = internal::Trim(value);
    if (trimmed.empty())
    {
        return std::nullopt;
    }

    std::size_t position = 0;
    bool negative = false;
    if (trimmed[position] == '-' || trimmed[position] == '+')
    {
        negative = trimmed[position] == '-';
        position++;
    }

    std::string body = trimmed.substr(position);
    std::int64_t fraction_ms = 0;
    const std::size_t dot = body.find('.');
    // A dot before the first colon separates days from hours (d.hh:mm:ss); a dot after the
    // last colon starts the fractional seconds.
    const std::size_t colon = body.find(':');
    if (dot != std::string::npos && (colon == std::string::npos || dot > colon))
    {
        const std::string fraction = body.substr(dot + 1);
        body = body.substr(0, dot);
        std::int64_t scale = 100;
        for (const char character : fraction)
        {
            if (character < '0' || character > '9')
            {
                return std::nullopt;
            }

            if (scale > 0)
            {
                fraction_ms += static_cast<std::int64_t>(character - '0') * scale;
                scale /= 10;
            }
        }
    }

    std::vector<std::string> parts;
    std::size_t Start = 0;
    for (;;)
    {
        const std::size_t separator = body.find(':', Start);
        if (separator == std::string::npos)
        {
            parts.push_back(body.substr(Start));
            break;
        }

        parts.push_back(body.substr(Start, separator - Start));
        Start = separator + 1;
    }

    if (parts.size() != 2 && parts.size() != 3)
    {
        return std::nullopt;
    }

    auto parse_component = [](const std::string& Text) -> std::optional<std::int64_t>
    {
        if (Text.empty())
        {
            return std::nullopt;
        }

        std::int64_t result = 0;
        for (const char character : Text)
        {
            if (character < '0' || character > '9')
            {
                return std::nullopt;
            }

            if (result > (std::numeric_limits<std::int64_t>::max() - (character - '0')) / 10)
            {
                return std::nullopt;
            }

            result = result * 10 + (character - '0');
        }

        return result;
    };

    const std::size_t count = parts.size();
    const auto seconds = parse_component(parts[count - 1]);
    const auto minutes = parse_component(parts[count - 2]);
    if (!seconds || !minutes || *seconds >= 60 || *minutes >= 60)
    {
        return std::nullopt;
    }

    std::int64_t hours = 0;
    std::int64_t days = 0;
    if (count == 3)
    {
        const std::size_t inner_dot = parts[0].find('.');
        if (inner_dot != std::string::npos)
        {
            const auto parsed_days = parse_component(parts[0].substr(0, inner_dot));
            const auto parsed_hours = parse_component(parts[0].substr(inner_dot + 1));
            if (!parsed_days || !parsed_hours || *parsed_hours >= 24)
            {
                return std::nullopt;
            }

            days = *parsed_days;
            hours = *parsed_hours;
        }
        else
        {
            const auto parsed_hours = parse_component(parts[0]);
            if (!parsed_hours)
            {
                return std::nullopt;
            }

            hours = *parsed_hours;
        }
    }

    // Overflow-checked accumulation: the days field and the hours field are unbounded digits.
    constexpr std::int64_t c_millisecondsPerDay = 24 * 60 * 60 * 1000;
    const auto add_scaled = [](std::int64_t& total, std::int64_t value, std::int64_t scale) -> bool
    {
        if (value > (std::numeric_limits<std::int64_t>::max() - total) / scale)
        {
            return false;
        }

        total += value * scale;
        return true;
    };

    std::int64_t total = 0;
    if (!add_scaled(total, days, c_millisecondsPerDay) || !add_scaled(total, hours, 60 * 60 * 1000) ||
        !add_scaled(total, *minutes, 60 * 1000) || !add_scaled(total, *seconds, 1000) ||
        !add_scaled(total, fraction_ms, 1))
    {
        return std::nullopt;
    }

    if (total <= 0)
    {
        return std::nullopt;
    }

    return std::chrono::milliseconds(negative ? -total : total);
}

} // namespace

std::optional<std::string> WslEnvironment::GetNonEmpty(const char* Name)
{
    const std::string value = internal::ReadEnvironmentVariable(Name);
    if (internal::IsBlank(value))
    {
        return std::nullopt;
    }

    return internal::Trim(value);
}

std::optional<bool> WslEnvironment::ParseBool(const std::optional<std::string>& value)
{
    if (!value)
    {
        return std::nullopt;
    }

    return internal::ParseBoolValue(*value);
}

std::optional<std::chrono::milliseconds> WslEnvironment::ParseTimeout(const std::optional<std::string>& value)
{
    // Accepted formats (matching the C# WslEnvironment.ParseTimeout contract): invariant
    // seconds as a double (> 0, e.g. "60", "1.5") or [d.]hh:mm:ss[.fff] clock time.
    // Inputs are trimmed (GetNonEmpty already trims) so surrounding whitespace is ignored.
    if (!value)
    {
        return std::nullopt;
    }

    const std::string text = internal::Trim(*value);
    if (text.empty())
    {
        return std::nullopt;
    }

    // std::from_chars is locale-independent (strtod honors the C locale) and rejects the hex
    // float syntax that C# double.TryParse also rejects. A leading '+' is allowed like C#.
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (begin != end && *begin == '+')
    {
        ++begin;
    }

    double seconds = 0.0;
    const auto [ptr, ec] = std::from_chars(begin, end, seconds);
    // Cap at TimeSpan.MaxValue.TotalSeconds so both ports accept the same range.
    constexpr double c_maxTimeoutSeconds = static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 10'000'000.0;
    if (ec == std::errc{} && ptr == end && std::isfinite(seconds) && seconds > 0 && seconds <= c_maxTimeoutSeconds)
    {
        return std::chrono::milliseconds(static_cast<std::int64_t>(seconds * 1000.0));
    }

    return ParseClockTime(text);
}

std::optional<std::string> WslEnvironment::DefaultImage()
{
    return GetNonEmpty(DefaultImageVariable);
}

const std::filesystem::path& WslEnvironment::DataDirectory()
{
    static const std::filesystem::path value = internal::ResolveDataDirectory(GetNonEmpty(DataDirectoryVariable));
    return value;
}

bool WslEnvironment::ReuseByDefault()
{
    return ParseBool(GetNonEmpty(ReuseVariable)).value_or(false);
}

bool WslEnvironment::CleanupEnabled()
{
    return ParseBool(GetNonEmpty(CleanupVariable)).value_or(true);
}

const std::string& WslEnvironment::SessionId()
{
    static const std::string value = internal::ResolveSessionId(GetNonEmpty(SessionIdVariable));
    return value;
}

std::chrono::milliseconds WslEnvironment::DefaultWaitTimeout()
{
    return ParseTimeout(GetNonEmpty(TimeoutVariable)).value_or(std::chrono::seconds(60));
}

bool WslEnvironment::ReuseAllowed()
{
    if (!internal::IsContinuousIntegration())
    {
        return true;
    }

    return ParseBool(GetNonEmpty(ReuseInCiVariable)).value_or(false);
}

} // namespace wslc
