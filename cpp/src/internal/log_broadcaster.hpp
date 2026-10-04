#pragma once

#include "wslc/log_line.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace wslc
{
class WslContainer;
}

namespace wslc::internal
{

/// <summary>Thread-safe fan-out log buffer with bounded history and bounded subscribers.</summary>
/// <remarks>
/// History is capped at 10k lines in a ring buffer and snapshots are cached until the Next
/// Publish, so readiness polls that call <c>GetRecentLogs</c> do not allocate a fresh vector on
/// every check. Each subscriber queue is bounded (1k, drop-oldest) so an abandoned stream cannot
/// grow memory without bound.
/// </remarks>
class LogBroadcaster
{
public:
    static constexpr std::size_t MaxHistory = 10'000;
    static constexpr std::size_t MaxSubscriberBuffered = 1'000;

    /// <summary>Queued lines and completion State for a single log stream.</summary>
    struct Subscriber
    {
        std::mutex Mutex;
        std::condition_variable Condition;
        std::deque<LogLine> Queue;
        bool Closed = false;
    };

    void Publish(const LogLine& line);
    std::shared_ptr<const std::vector<LogLine>> Snapshot();
    std::shared_ptr<Subscriber> Subscribe();
    void Unsubscribe(const std::shared_ptr<Subscriber>& subscriber);
    void Complete();

private:
    std::mutex m_gate;
    std::vector<LogLine> m_history = std::vector<LogLine>(MaxHistory);
    std::vector<std::shared_ptr<Subscriber>> m_subscribers;
    std::size_t m_head = 0;
    std::size_t m_count = 0;
    std::uint64_t m_version = 0;
    std::shared_ptr<const std::vector<LogLine>> m_snapshot;
    std::uint64_t m_snapshotVersion = 0;
    bool m_hasSnapshot = false;
    bool m_completed = false;
};

} // namespace wslc::internal
