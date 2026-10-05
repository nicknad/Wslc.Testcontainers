#include "wslc/exceptions.hpp"

#include "internal/text_truncation.hpp"
#include "internal/util.hpp"

#include <format>
#include <vector>

namespace wslc
{

namespace
{

std::vector<std::string> SplitLines(const std::string& value)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = value.find('\n', start);
        if (end == std::string::npos)
        {
            lines.push_back(value.substr(start));
            break;
        }

        lines.push_back(value.substr(start, end - start));
        start = end + 1;
    }

    return lines;
}

} // namespace

WslReadinessException::WslReadinessException(std::string message, std::string ExpectedCondition,
                                             std::chrono::milliseconds Timeout, std::vector<LogLine> Logs)
    : WslTimeoutException(message), m_expectedCondition(std::move(ExpectedCondition)), m_timeout(Timeout),
      m_logs(std::move(Logs))
{
}

WslReadinessException WslReadinessException::WithDiagnostics(std::optional<std::string> Image,
                                                             std::optional<std::string> command,
                                                             std::optional<int> ExitCode,
                                                             std::optional<std::string> StdoutText,
                                                             std::optional<std::string> StderrText) const
{
    WslReadinessException result(what(), m_expectedCondition, m_timeout, m_logs);
    if (Image)
    {
        result.m_image = std::move(Image);
    }
    else
    {
        result.m_image = m_image;
    }

    if (command)
    {
        result.m_command = std::move(command);
    }
    else
    {
        result.m_command = m_command;
    }

    result.m_exitCode = ExitCode ? ExitCode : m_exitCode;
    if (StdoutText)
    {
        result.m_stdoutText = std::move(StdoutText);
    }
    else
    {
        result.m_stdoutText = m_stdoutText;
    }

    if (StderrText)
    {
        result.m_stderrText = std::move(StderrText);
    }
    else
    {
        result.m_stderrText = m_stderrText;
    }

    return result;
}

std::string WslReadinessException::Describe() const
{
    std::string header = std::format("WSLC readiness failed\n\nImage:        {}\nCommand:      {}\n",
                                     m_image.value_or("<unknown>"), m_command.value_or("<none>"));
    if (!m_command)
    {
        header += "Hint:         no init command was configured, so only a keep-alive shell is running.\n"
                  "              WSLC never runs the Image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or "
                  "use a module builder.\n";
    }

    header += std::format("Expected:     {}\nTimeout:      {}s\n", m_expectedCondition,
                          internal::FormatMilliseconds(m_timeout));
    if (m_exitCode)
    {
        header += std::format("Exit code:    {}\n", *m_exitCode);
    }

    const auto not_blank = [](const std::optional<std::string>& value)
    { return value.has_value() && !internal::IsBlank(*value); };

    std::string body;
    if (not_blank(m_stdoutText))
    {
        body += "\nLast stdout:\n" +
                internal::CapLines(SplitLines(*m_stdoutText), internal::c_maxLineBytes, internal::c_maxSectionBytes) +
                "\n";
    }

    if (not_blank(m_stderrText))
    {
        body += "\nLast stderr:\n" +
                internal::CapLines(SplitLines(*m_stderrText), internal::c_maxLineBytes, internal::c_maxSectionBytes) +
                "\n";
    }

    if (!m_logs.empty())
    {
        std::vector<std::string> lines;
        lines.reserve(m_logs.size());
        for (const auto& line : m_logs)
        {
            lines.push_back(line.ToString());
        }

        body += "\nRecent Logs:\n" + internal::CapLines(lines, internal::c_maxLineBytes, internal::c_maxSectionBytes) +
                "\n";
    }

    if (header.size() >= internal::c_maxDescribeBytes)
    {
        return header;
    }

    const std::size_t remaining = internal::c_maxDescribeBytes - header.size();
    if (body.size() > remaining)
    {
        body = internal::CapText(body, remaining);
    }

    return header + body;
}

} // namespace wslc
