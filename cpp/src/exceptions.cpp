#include "wslc/exceptions.hpp"

#include "internal/util.hpp"

#include <sstream>

namespace wslc
{

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
    std::ostringstream builder;
    builder << "WSLC readiness failed\n\n";
    builder << "Image:        " << (m_image ? *m_image : "<unknown>") << '\n';
    builder << "Command:      " << (m_command ? *m_command : "<none>") << '\n';
    if (!m_command)
    {
        builder << "Hint:         no init command was configured, so only a keep-alive shell is running.\n";
        builder << "              WSLC never runs the Image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or "
                   "use a module builder.\n";
    }

    builder << "Expected:     " << m_expectedCondition << '\n';
    builder << "Timeout:      " << internal::FormatMilliseconds(m_timeout) << "s\n";

    if (m_exitCode)
    {
        builder << "Exit code:    " << *m_exitCode << '\n';
    }

    const auto not_blank = [](const std::optional<std::string>& value)
    { return value.has_value() && !internal::IsBlank(*value); };

    if (not_blank(m_stdoutText))
    {
        builder << "\nLast stdout:\n" << *m_stdoutText << '\n';
    }

    if (not_blank(m_stderrText))
    {
        builder << "\nLast stderr:\n" << *m_stderrText << '\n';
    }

    if (!m_logs.empty())
    {
        builder << "\nRecent Logs:\n";
        for (const auto& line : m_logs)
        {
            builder << line.ToString() << '\n';
        }
    }

    return builder.str();
}

} // namespace wslc
