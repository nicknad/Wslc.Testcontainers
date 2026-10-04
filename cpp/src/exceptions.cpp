#include "wslc/exceptions.hpp"

#include "internal/util.hpp"

#include <format>

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
    std::string result = std::format("WSLC readiness failed\n\nImage:        {}\nCommand:      {}\n",
                                     m_image.value_or("<unknown>"), m_command.value_or("<none>"));
    if (!m_command)
    {
        result += "Hint:         no init command was configured, so only a keep-alive shell is running.\n"
                  "              WSLC never runs the Image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or "
                  "use a module builder.\n";
    }

    result += std::format("Expected:     {}\nTimeout:      {}s\n", m_expectedCondition,
                          internal::FormatMilliseconds(m_timeout));
    if (m_exitCode)
    {
        result += std::format("Exit code:    {}\n", *m_exitCode);
    }

    const auto not_blank = [](const std::optional<std::string>& value)
    { return value.has_value() && !internal::IsBlank(*value); };

    if (not_blank(m_stdoutText))
    {
        result += std::format("\nLast stdout:\n{}\n", *m_stdoutText);
    }

    if (not_blank(m_stderrText))
    {
        result += std::format("\nLast stderr:\n{}\n", *m_stderrText);
    }

    if (!m_logs.empty())
    {
        result += "\nRecent Logs:\n";
        for (const auto& line : m_logs)
        {
            result += std::format("{}\n", line.ToString());
        }
    }

    return result;
}

} // namespace wslc
