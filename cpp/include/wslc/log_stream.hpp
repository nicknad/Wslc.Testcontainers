#pragma once

#include "wslc/log_line.hpp"

#include <memory>
#include <optional>
#include <stop_token>

namespace wslc
{
class WslContainer;
}

namespace wslc::internal
{
class LogBroadcaster;
}

namespace wslc
{

/// <summary>
/// A live view over the container's captured Logs. <c>Next</c> blocks until a line is available,
/// the stream completes, or the Stop token fires; the stream replays retained history
/// oldest-first on creation. Callers should let the Object go out of scope (or call
/// <c>Cancel</c>) when done so the subscription is released. The stream is move-only.
/// </summary>
class LogStream
{
public:
    LogStream();
    ~LogStream();
    LogStream(LogStream&& other) noexcept;
    LogStream& operator=(LogStream&& other) noexcept;
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    /// <summary>Blocks for the Next line; returns nullopt when the stream completed or the token fired.</summary>
    std::optional<LogLine> Next(std::stop_token token = {});

    /// <summary>Releases the subscription. Idempotent; <c>Next</c> afterwards returns nullopt.</summary>
    void Cancel();

private:
    friend class WslContainer;
    static LogStream FromBroadcaster(std::shared_ptr<internal::LogBroadcaster> broadcaster);
    struct Impl;
    explicit LogStream(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace wslc
