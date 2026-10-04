#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace wslc::internal
{

/// <summary>
/// Thread-safe capture buffer that keeps only the newest bytes. Long-running processes can emit
/// unbounded output; the buffer is a fixed-size ring so appending past the cap drops the oldest
/// bytes in place without whole-buffer copies.
/// </summary>
class CaptureBuffer
{
public:
    static constexpr std::size_t MaxBytes = 1024 * 1024;

    void Append(std::span<const std::uint8_t> data)
    {
        if (data.empty())
        {
            return;
        }

        std::lock_guard lock(m_gate);
        if (m_disposed)
        {
            return;
        }

        if (data.size() >= m_buffer.size())
        {
            std::copy(data.end() - static_cast<std::ptrdiff_t>(m_buffer.size()), data.end(), m_buffer.begin());
            m_start = 0;
            m_count = static_cast<int>(m_buffer.size());
            return;
        }

        const int overflow = m_count + static_cast<int>(data.size()) - static_cast<int>(m_buffer.size());
        if (overflow > 0)
        {
            m_count -= overflow;
            m_start = (m_start + overflow) % static_cast<int>(m_buffer.size());
        }

        const int tail = (m_start + m_count) % static_cast<int>(m_buffer.size());
        const std::size_t first = std::min(data.size(), m_buffer.size() - static_cast<std::size_t>(tail));
        std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(first), m_buffer.begin() + tail);
        if (first < data.size())
        {
            std::copy(data.begin() + static_cast<std::ptrdiff_t>(first), data.end(), m_buffer.begin());
        }

        m_count += static_cast<int>(data.size());
    }

    std::string Decode() const
    {
        std::lock_guard lock(m_gate);
        if (m_count == 0)
        {
            return {};
        }

        if (m_start + m_count <= static_cast<int>(m_buffer.size()))
        {
            return std::string(reinterpret_cast<const char*>(m_buffer.data() + m_start),
                               static_cast<std::size_t>(m_count));
        }

        std::string result(static_cast<std::size_t>(m_count), '\0');
        const int first = static_cast<int>(m_buffer.size()) - m_start;
        std::copy(m_buffer.begin() + m_start, m_buffer.end(), result.begin());
        std::copy(m_buffer.begin(), m_buffer.begin() + (m_count - first), result.begin() + first);
        return result;
    }

    void Dispose()
    {
        std::lock_guard lock(m_gate);
        m_disposed = true;
        m_start = 0;
        m_count = 0;
    }

private:
    // Heap-allocated: a by-value 1 MiB ring would overflow the default thread stack.
    std::vector<std::uint8_t> m_buffer = std::vector<std::uint8_t>(MaxBytes);
    mutable std::mutex m_gate;
    int m_start = 0;
    int m_count = 0;
    bool m_disposed = false;
};

} // namespace wslc::internal
