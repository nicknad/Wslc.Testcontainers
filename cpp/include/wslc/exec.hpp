#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace wslc
{

/// <summary>Optional settings shared by Exec() and StartProcess().</summary>
/// <remarks>
/// Pass a default-constructed value for defaults. ExecOptions derives from this type, so it
/// can be passed anywhere a ProcessOptions is accepted; StartProcess uses RTTI to reject an
/// ExecOptions carrying StandardInput/Timeout (which apply only to Exec) with WslcException.
/// The virtual destructor makes this type polymorphic for that runtime check.
/// </remarks>
struct ProcessOptions
{
    /// <summary>Environment variables for the command. Overrides container-level variables with the same
    /// name.</summary>
    std::map<std::string, std::string> Environment;

    /// <summary>Linux working directory for the command. Must be non-empty when set.</summary>
    std::optional<std::string> WorkingDirectory;

    virtual ~ProcessOptions() = default;
};

/// <summary>Optional settings for Exec().</summary>
/// <remarks>
/// <c>StandardInput</c> and <c>Timeout</c> apply only to Exec(); passing an ExecOptions with
/// either set to StartProcess() (which accepts ProcessOptions) throws WslcException.
/// </remarks>
struct ExecOptions : ProcessOptions
{
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
