#pragma once

#include <chrono>
#include <string>

namespace wslc
{

/// <summary>Identifies the origin of a log line.</summary>
enum class LogSource
{
    /// <summary>Standard output of a process.</summary>
    Stdout,

    /// <summary>Standard error of a process.</summary>
    Stderr,

    /// <summary>Diagnostic output produced by the library itself.</summary>
    System,
};

/// <summary>A single captured log line.</summary>
struct LogLine
{
    LogSource Source = LogSource::System;
    std::string Text;
    std::chrono::system_clock::time_point Timestamp;

    /// <summary>Creates a Diagnostic line stamped with the current time.</summary>
    static LogLine Diagnostic(std::string text);

    /// <summary>Renders "HH:mm:ss.fff [Source] Text".</summary>
    std::string ToString() const;
};

} // namespace wslc
