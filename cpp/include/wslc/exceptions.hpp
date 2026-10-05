#pragma once

#include "wslc/log_line.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wslc
{

/// <summary>
/// Base exception for all WSLC failures, including invalid builder configuration reported by
/// Build() and operations attempted before Start().
/// </summary>
class WslcException : public std::runtime_error
{
public:
    explicit WslcException(const std::string& message) : std::runtime_error(message) {}
};

/// <summary>The current OS or architecture cannot Run WSLC.</summary>
class PlatformNotSupportedException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>WSL itself is missing, disabled or misconfigured.</summary>
class WslRuntimeException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>Creating or preparing the WSL Environment failed.</summary>
class WslProvisioningException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>A command or process inside the Environment failed.</summary>
class WslProcessException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>An operation exceeded its configured Timeout.</summary>
class WslTimeoutException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>Port forwarding or address resolution failed.</summary>
class WslNetworkException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>Cleanup of a WSLC Environment failed.</summary>
class WslCleanupException : public WslcException
{
public:
    using WslcException::WslcException;
};

/// <summary>Thrown when a caller-provided Stop token is triggered.</summary>
class OperationCanceledException : public WslcException
{
public:
    OperationCanceledException() : WslcException("The operation was canceled.") {}
};

/// <summary>A readiness (Wait) strategy did not become satisfied in time.</summary>
class WslReadinessException : public WslTimeoutException
{
public:
    WslReadinessException(std::string message, std::string expectedCondition, std::chrono::milliseconds timeout,
                          std::vector<LogLine> logs);

    /// <summary>The condition that was awaited.</summary>
    const std::string& ExpectedCondition() const noexcept { return m_expectedCondition; }

    /// <summary>The configured Timeout.</summary>
    std::chrono::milliseconds Timeout() const noexcept { return m_timeout; }

    /// <summary>
    /// Recent log lines captured when the Wait failed. Captured output is untrusted, may contain
    /// secrets, and is truncated by Describe().
    /// </summary>
    const std::vector<LogLine>& Logs() const noexcept { return m_logs; }

    /// <summary>The container Image reference, when known.</summary>
    const std::optional<std::string>& Image() const noexcept { return m_image; }

    /// <summary>The configured command, when any.</summary>
    const std::optional<std::string>& command() const noexcept { return m_command; }

    /// <summary>The exit code of the main process, when it had already exited.</summary>
    std::optional<int> ExitCode() const noexcept { return m_exitCode; }

    /// <summary>
    /// Captured standard output tail. Captured output is untrusted, may contain secrets, and is
    /// truncated by Describe().
    /// </summary>
    const std::optional<std::string>& StdoutText() const noexcept { return m_stdoutText; }

    /// <summary>
    /// Captured standard error tail. Captured output is untrusted, may contain secrets, and is
    /// truncated by Describe().
    /// </summary>
    const std::optional<std::string>& StderrText() const noexcept { return m_stderrText; }

    /// <summary>Returns a copy with the container diagnostics filled in.</summary>
    WslReadinessException WithDiagnostics(std::optional<std::string> Image, std::optional<std::string> command,
                                          std::optional<int> ExitCode, std::optional<std::string> StdoutText,
                                          std::optional<std::string> StderrText) const;

    /// <summary>
    /// Renders the full Diagnostic report required for failed readiness. Captured output is
    /// untrusted, may contain secrets, and can be arbitrarily large: each captured section is
    /// capped at 8 KiB, each line at 4 KiB, and the whole report at 64 KiB. The header is never
    /// truncated.
    /// </summary>
    std::string Describe() const;

private:
    std::string m_expectedCondition;
    std::chrono::milliseconds m_timeout;
    std::vector<LogLine> m_logs;
    std::optional<std::string> m_image;
    std::optional<std::string> m_command;
    std::optional<int> m_exitCode;
    std::optional<std::string> m_stdoutText;
    std::optional<std::string> m_stderrText;
};

} // namespace wslc
