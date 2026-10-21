#include "internal/log_broadcaster.hpp"

#include "internal/text_truncation.hpp"

namespace wslc::internal
{

void LogBroadcaster::Publish(const LogLine& line)
{
    const LogLine capped =
        line.Text.size() > MaxLineBytes ? LogLine{line.Source, CapText(line.Text, MaxLineBytes), line.Timestamp} : line;

    std::lock_guard lock(m_gate);
    if (m_completed)
    {
        return;
    }

    m_history[(m_head + m_count) % MaxHistory] = capped;
    if (m_count == MaxHistory)
    {
        m_head = (m_head + 1) % MaxHistory;
    }
    else
    {
        m_count++;
    }

    m_version++;

    for (const auto& subscriber : m_subscribers)
    {
        std::lock_guard subscriber_lock(subscriber->Mutex);
        subscriber->Queue.push_back(capped);
        if (subscriber->Queue.size() > MaxSubscriberBuffered)
        {
            subscriber->Queue.pop_front();
        }
    }

    for (const auto& subscriber : m_subscribers)
    {
        subscriber->Condition.notify_all();
    }
}

std::shared_ptr<const std::vector<LogLine>> LogBroadcaster::Snapshot()
{
    std::lock_guard lock(m_gate);
    if (!m_hasSnapshot || m_snapshotVersion != m_version)
    {
        auto lines = std::make_shared<std::vector<LogLine>>();
        lines->reserve(m_count);
        for (std::size_t i = 0; i < m_count; i++)
        {
            lines->push_back(m_history[(m_head + i) % MaxHistory]);
        }

        m_snapshot = lines;
        m_snapshotVersion = m_version;
        m_hasSnapshot = true;
    }

    return m_snapshot;
}

std::shared_ptr<LogBroadcaster::Subscriber> LogBroadcaster::Subscribe()
{
    auto subscriber = std::make_shared<Subscriber>();
    std::lock_guard lock(m_gate);
    // Replay only the newest lines: the queue is drop-oldest bounded, so a subscriber attached
    // after a long run must not start with more than MaxSubscriberBuffered entries.
    const std::size_t first = m_count > MaxSubscriberBuffered ? m_count - MaxSubscriberBuffered : 0;
    for (std::size_t i = first; i < m_count; i++)
    {
        subscriber->Queue.push_back(m_history[(m_head + i) % MaxHistory]);
    }

    if (m_completed)
    {
        subscriber->Closed = true;
        return subscriber;
    }

    m_subscribers.push_back(subscriber);
    return subscriber;
}

void LogBroadcaster::Unsubscribe(const std::shared_ptr<Subscriber>& subscriber)
{
    {
        std::lock_guard lock(m_gate);
        for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it)
        {
            if (it->get() == subscriber.get())
            {
                m_subscribers.erase(it);
                break;
            }
        }
    }

    {
        std::lock_guard subscriber_lock(subscriber->Mutex);
        subscriber->Closed = true;
    }

    subscriber->Condition.notify_all();
}

void LogBroadcaster::Complete()
{
    std::vector<std::shared_ptr<Subscriber>> subscribers;
    {
        std::lock_guard lock(m_gate);
        if (m_completed)
        {
            return;
        }

        m_completed = true;
        subscribers.swap(m_subscribers);
    }

    for (const auto& subscriber : subscribers)
    {
        {
            std::lock_guard subscriber_lock(subscriber->Mutex);
            subscriber->Closed = true;
        }

        subscriber->Condition.notify_all();
    }
}

} // namespace wslc::internal
