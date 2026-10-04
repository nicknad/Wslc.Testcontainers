#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace wslc::internal
{

/// <summary>
/// Incrementally assembles byte chunks into Complete lines (newline-delimited, trimming a
/// trailing carriage return). Pending data is capped: a giant line without a newline is emitted
/// early so memory stays bounded. Because UTF-8 continuation bytes never contain 0x0A, line
/// splitting can operate on raw bytes and multibyte characters split across chunks join when
/// the Next chunk arrives.
/// </summary>
class LineAssembler
{
public:
    static constexpr std::size_t MaxPendingBytes = 256 * 1024;

    explicit LineAssembler(std::function<void(std::string)> on_line) : m_onLine(std::move(on_line)) {}

    void Append(std::span<const std::uint8_t> data)
    {
        m_pending.append(reinterpret_cast<const char*>(data.data()), data.size());
        emit_complete_lines();

        if (m_pending.size() > MaxPendingBytes)
        {
            m_onLine(std::move(m_pending));
            m_pending.clear();
            m_scanFrom = 0;
        }
    }

    void Flush()
    {
        emit_complete_lines();
        if (!m_pending.empty())
        {
            m_onLine(std::move(m_pending));
            m_pending.clear();
        }

        m_scanFrom = 0;
    }

private:
    void emit_complete_lines()
    {
        // Scanning resumes where the previous call stopped: earlier bytes are known to contain
        // no newline, so a long line spanning many chunks is scanned once.
        std::size_t line_start = 0;
        for (std::size_t i = m_scanFrom; i < m_pending.size(); i++)
        {
            if (m_pending[i] != '\n')
            {
                continue;
            }

            const std::size_t end = i > 0 && m_pending[i - 1] == '\r' ? i - 1 : i;
            m_onLine(m_pending.substr(line_start, end - line_start));
            line_start = i + 1;
        }

        if (line_start > 0)
        {
            m_pending.erase(0, line_start);
        }

        m_scanFrom = m_pending.size();
    }

    std::function<void(std::string)> m_onLine;
    std::string m_pending;
    std::size_t m_scanFrom = 0;
};

} // namespace wslc::internal
