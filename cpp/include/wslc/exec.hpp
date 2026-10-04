#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace wslc
{

/// <summary>Optional settings for Exec() and StartProcess().</summary>
/// <remarks>
/// <c>StandardInput</c> and <c>Timeout</c> apply only to Exec(); StartProcess() rejects them
/// with <see cref="WslcException"/>.
/// </remarks>
struct ExecOptions
{
    /// <summary>Environment variables for the command. Overrides container-level variables with the same
    /// Name.</summary>
    std::map<std::string, std::string> Environment;

    /// <summary>Linux working directory for the command. Must be non-empty when set.</summary>
    std::optional<std::string> WorkingDirectory;

    /// <summary>Text written to the command standard input (Exec only).</summary>
    std::optional<std::string> StandardInput;

    /// <summary>Maximum execution time. Must be positive and at most 24 hours when set; a
    /// WslTimeoutException is thrown when exceeded.</summary>
    std::optional<std::chrono::milliseconds> Timeout;
};

/// <summary>Result of executing a command inside a WSLC Environment.</summary>
struct ExecResult
{
    /// <summary>The Linux process exit code.</summary>
    int ExitCode = 0;

    /// <summary>Captured standard output.</summary>
    std::string StdoutText;

    /// <summary>Captured standard error.</summary>
    std::string StderrText;

    /// <summary>Gets a value indicating whether the command exited successfully.</summary>
    bool Succeeded() const noexcept { return ExitCode == 0; }

    /// <summary>Throws a WslProcessException when the command failed.</summary>
    const ExecResult& EnsureSuccess() const;
};

} // namespace wslc
