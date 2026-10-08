#include "wslc/log_stream.hpp"

#include "internal/log_broadcaster.hpp"

namespace wslc
{

struct LogStream::Impl
{
    std::shared_ptr<internal::LogBroadcaster> broadcaster;
    std::shared_ptr<internal::LogBroadcaster::Subscriber> subscriber;
    bool canceled = false;
};

LogStream::LogStream() = default;

LogStream::LogStream(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

LogStream::~LogStream()
{
    Cancel();
}

LogStream::LogStream(LogStream&& other) noexcept = default;

LogStream& LogStream::operator=(LogStream&& other) noexcept
{
    if (this != &other)
    {
        Cancel();
        m_impl = std::move(other.m_impl);
    }

    return *this;
}

std::optional<LogLine> LogStream::Next(std::stop_token token)
{
    if (m_impl == nullptr || m_impl->subscriber == nullptr)
    {
        return std::nullopt;
    }

    auto& subscriber = *m_impl->subscriber;
    std::unique_lock lock(subscriber.Mutex);
    if (!subscriber.Queue.empty())
    {
        LogLine line = std::move(subscriber.Queue.front());
        subscriber.Queue.pop_front();
        return line;
    }

    if (subscriber.Closed || token.stop_requested())
    {
        return std::nullopt;
    }

    // condition_variable_any's stop_token overload closes the missed-wakeup race between a
    // stop request and the predicate check.
    subscriber.Condition.wait(lock, token, [&subscriber] { return !subscriber.Queue.empty() || subscriber.Closed; });

    if (!subscriber.Queue.empty())
    {
        LogLine line = std::move(subscriber.Queue.front());
        subscriber.Queue.pop_front();
        return line;
    }

    return std::nullopt;
}

void LogStream::Cancel()
{
    if (m_impl == nullptr || m_impl->canceled)
    {
        return;
    }

    m_impl->canceled = true;
    m_impl->broadcaster->Unsubscribe(m_impl->subscriber);
    m_impl->subscriber.reset();
}

LogStream LogStream::FromBroadcaster(std::shared_ptr<internal::LogBroadcaster> broadcaster)
{
    auto impl = std::make_unique<Impl>();
    impl->subscriber = broadcaster->Subscribe();
    impl->broadcaster = std::move(broadcaster);
    return LogStream(std::move(impl));
}

} // namespace wslc
