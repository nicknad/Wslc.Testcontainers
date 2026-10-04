#include "wslc/log_line.hpp"

#include <cstdio>
#include <ctime>

namespace wslc
{

namespace
{

std::time_t ToTimeT(std::chrono::system_clock::time_point value)
{
    return std::chrono::system_clock::to_time_t(value);
}

const char* SourceName(LogSource Source)
{
    switch (Source)
    {
    case LogSource::Stdout:
        return "stdout";
    case LogSource::Stderr:
        return "stderr";
    default:
        return "system";
    }
}

} // namespace

LogLine LogLine::Diagnostic(std::string Text)
{
    return LogLine{LogSource::System, std::move(Text), std::chrono::system_clock::now()};
}

std::string LogLine::ToString() const
{
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(Timestamp);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(Timestamp - seconds).count();
    const std::time_t time = ToTimeT(seconds);
    std::tm utc{};
    gmtime_s(&utc, &time);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", utc.tm_hour, utc.tm_min, utc.tm_sec,
                  static_cast<int>(millis));

    std::string result(buffer);
    result += " [";
    result += SourceName(Source);
    result += "] ";
    result += Text;
    return result;
}

} // namespace wslc
