#pragma once

#include "wslc/exec.hpp"
#include "wslc/log_line.hpp"
#include "wslc/log_stream.hpp"
#include "wslc/process.hpp"
#include "wslc/waiting/i_wait_target.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace wslc
{

class WslContainerBuilder;

namespace internal
{
struct Configuration;
}

/// <summary>
/// A disposable, isolated WSL container managed by WSLC on top of the official
/// wslcsdk API. Create instances through <see cref="WslContainerBuilder"/>, then call
/// <c>Start</c>. The type is move-only; destroying it performs the same Cleanup as
/// <c>Dispose</c>.
/// </summary>
/// <remarks>
/// Lifecycle transitions (Start/Stop/Dispose) are serialized. Commands, copies and
/// long-running processes are intentionally not gated: starting one while the container stops
/// aborts that operation with an exception instead of corrupting State.
/// </remarks>
class WslContainer final : public waiting::IWaitTarget
{
public:
    WslContainer(const WslContainer&) = delete;
    WslContainer& operator=(const WslContainer&) = delete;
    WslContainer(WslContainer&& other) noexcept;
    WslContainer& operator=(WslContainer&& other) noexcept;
    ~WslContainer() override;

    /// <summary>Gets the unique Name of the underlying WSLC instance (IWslContainer.Name).</summary>
    const std::string& Name() const override;

    /// <summary>Gets the container Image reference, when one was configured.</summary>
    std::optional<std::string> Image() const;

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    bool IsStarted() const;

    /// <summary>
    /// Gets a value indicating whether Reuse will actually be used for this container: Reuse
    /// must be configured (or enabled through WSLC_REUSE) and not suppressed by the Environment
    /// (Reuse is forced off under CI unless WSLC_REUSE_IN_CI is truthy).
    /// </summary>
    bool IsReuseEffective() const;

    /// <summary>
    /// Gets the Windows endpoint to connect to for a mapped Linux TCP port: the runtime-assigned
    /// Host port and the port's configured bind address. The default/wildcard binding resolves to
    /// loopback: IPv4 127.0.0.1 for 0.0.0.0 and IPv6 ::1 for ::.
    /// </summary>
    /// <exception cref="WslException">The container has not been started.</exception>
    /// <exception cref="WslNetworkException">The port was not declared with WithPort or the runtime
    /// has not assigned it.</exception>
    WslEndpoint GetConnectEndpoint(int containerPort) const override;

    /// <summary>Creates and provisions the Environment, then waits until all readiness Strategies pass.</summary>
    void Start(std::stop_token token = {});

    /// <summary>
    /// Stops all processes and terminates the WSLC session. On-disk session storage and metadata
    /// are preserved so a subsequent Start restarts the same instance Name from a clean storage
    /// directory. Destruction deletes ephemeral storage; reusable instances are preserved.
    /// </summary>
    void Stop(std::stop_token token = {});

    /// <summary>Executes a command and captures its exit code, stdout and stderr. At most 1000 arguments may be
    /// passed.</summary>
    ExecResult Exec(std::string command, std::vector<std::string> arguments = {}, ExecOptions options = {},
                    std::stop_token token = {});

    /// <summary>IWaitTarget surface: executes a command with container defaults.</summary>
    ExecResult Exec(std::string command, std::vector<std::string> arguments, std::stop_token token) override;

    /// <summary>Runs a script through the container shell (/bin/sh -c) and captures its exit code,
    /// stdout and stderr. Convenience for pipelines, redirection, globbing and shell builtins that
    /// Exec cannot express because it does not use a shell. The same argument limits as Exec apply.</summary>
    ExecResult ExecShell(std::string script, ExecOptions options = {}, std::stop_token token = {});

    /// <summary>
    /// Starts a long-running process inside the Environment. The caller owns the returned
    /// Handle; destroying it terminates a still-running process. At most 1000 arguments may be
    /// passed; options carry only the working directory and per-process Environment.
    /// </summary>
    /// <exception cref="WslException">options is an ExecOptions carrying StandardInput or Timeout;
    /// those apply only to Exec().</exception>
    std::unique_ptr<IWslProcess> StartProcess(std::string command, std::vector<std::string> arguments = {},
                                              const ProcessOptions& options = {}, std::stop_token token = {});

    /// <summary>
    /// Copies a Windows file into the Environment. Fails when the Source is missing or larger
    /// than 1 GiB. The container path must be an absolute Linux path.
    /// </summary>
    void CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token = {});

    /// <summary>Copies a Linux file out of the Environment to a Windows path. Creates parent directories.</summary>
    void CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath, std::stop_token token = {});

    /// <summary>Subscribes to all Logs captured by the Environment, replaying retained history first.</summary>
    LogStream SubscribeLogs();

    /// <summary>Returns a bounded Snapshot of the most recent log lines, oldest first.</summary>
    std::vector<LogLine> GetRecentLogs() const override;

    /// <summary>Returns the trailing <paramref name="maxLines"/> lines, oldest first.</summary>
    std::vector<LogLine> GetRecentLogs(int maxLines) const;

    /// <summary>
    /// Stops and releases the container, deleting ephemeral storage. Idempotent; also runs from
    /// the destructor.
    /// </summary>
    void Dispose();

    // IWaitTarget surface used by Wait Strategies; not part of the primary API path.
    bool IsTcpPortOpen(int containerPort, std::stop_token token) override;
    bool IsProcessRunning(std::string processName, std::stop_token token) override;

private:
    friend class WslContainerBuilder;
    struct Impl;
    explicit WslContainer(std::unique_ptr<Impl> impl);
    static WslContainer Create(internal::Configuration configuration);

    std::unique_ptr<Impl> m_impl;
};

} // namespace wslc
