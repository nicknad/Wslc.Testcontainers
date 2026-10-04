#pragma once

#include <optional>
#include <stop_token>

namespace wslc
{

/// <summary>
/// A long-running process started inside a WSLC Environment. The caller owns the Handle:
/// destroying it (or calling <c>Dispose</c>) terminates a still-running process, so disposal
/// both releases the Handle and cleans the process up. Calling <c>Dispose</c> twice is safe.
/// </summary>
class IWslProcess
{
public:
    virtual ~IWslProcess() = default;

    /// <summary>Gets the Linux process Id when it could be resolved.</summary>
    virtual std::optional<int> Id() const = 0;

    /// <summary>Gets a value indicating whether the process has exited.</summary>
    virtual bool HasExited() const = 0;

    /// <summary>Gets the process exit code. Throws when the process has not exited.</summary>
    virtual int ExitCode() const = 0;

    /// <summary>Waits until the process exits and returns its exit code.</summary>
    virtual int WaitForExit(std::stop_token token = {}) = 0;

    /// <summary>Terminates the process (SIGTERM, then SIGKILL after a grace period).</summary>
    virtual void Kill(std::stop_token token = {}) = 0;

    /// <summary>Releases the Handle, terminating the process when it is still running. Idempotent.</summary>
    virtual void Dispose() = 0;
};

} // namespace wslc
