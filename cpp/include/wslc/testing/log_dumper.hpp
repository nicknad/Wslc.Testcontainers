#pragma once

#include "wslc/log_stream.hpp"

#include <functional>
#include <stop_token>
#include <string>

namespace wslc::testing
{

/// <summary>
/// Dumps bounded log output to any sink (console, file, test harness). Takes a callable so the
/// library does not depend on any test framework.
/// </summary>
class LogDumper
{
public:
    /// <summary>
    /// Dumps the head of a live stream — up to <paramref Name="maxLines"/> lines, oldest first —
    /// then stops. The stream replays retained history oldest-first, so this returns the oldest
    /// lines, not the latest; use <c>GetRecentLogs</c> for a bounded tail Snapshot.
    /// </summary>
    static void DumpHead(LogStream& Logs, const std::function<void(const std::string&)>& write_line, int maxLines = 100,
                         std::stop_token token = {});
};

} // namespace wslc::testing
