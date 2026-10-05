using System.Net;

namespace Wslc.Testcontainers;

/// <summary>
/// A disposable, isolated WSL container managed by WSLC on top of the official
/// <c>Microsoft.WSL.Containers</c> API.
/// </summary>
/// <remarks>
/// A library-implemented service interface (per ADR-0008): consumers program against it but
/// should not implement it, because additive members would break external implementers.
/// Extend behavior via <see cref="Waiting.IWaitStrategy"/> (including custom
/// <c>Wait.ForWsl().Until</c> conditions), module builders, or the builders' configuration hooks.
/// </remarks>
public interface IWslContainer : IAsyncDisposable
{
    /// <summary>Gets the unique name of the underlying WSLC instance.</summary>
    string Name { get; }

    /// <summary>Gets the container image reference, when one was configured.</summary>
    string? Image { get; }

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    bool IsStarted { get; }

    /// <summary>
    /// Gets a value indicating whether reuse will actually be used for this container: reuse
    /// must be configured (or enabled through <c>WSLC_REUSE</c>) and not suppressed by the
    /// environment (reuse is forced off under CI unless <c>WSLC_REUSE_IN_CI</c> is truthy).
    /// </summary>
    bool IsReuseEffective { get; }

    /// <summary>
    /// Gets the Windows endpoint to connect to for a mapped Linux TCP port: the runtime-assigned
    /// host port and the port's configured bind address. The default/wildcard binding resolves to
    /// loopback: IPv4 <c>127.0.0.1</c> for <c>0.0.0.0</c> and IPv6 <c>::1</c> for <c>::</c>.
    /// </summary>
    /// <exception cref="WslException">The container has not been started.</exception>
    /// <exception cref="WslNetworkException">The port was not declared with <c>WithPort</c> or the runtime has not assigned it.</exception>
    IPEndPoint GetConnectEndpoint(int containerPort);

    /// <summary>Creates and provisions the environment, then waits until all readiness strategies pass.</summary>
    /// <remarks>
    /// May be called again after <see cref="StopAsync"/> to re-provision the same instance.
    /// Session storage is reset on restart so the runtime always starts from an empty directory.
    /// </remarks>
    Task StartAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Stops all processes and terminates the WSLC session. On-disk session storage and metadata
    /// are preserved so a subsequent <see cref="StartAsync"/> restarts the same instance name
    /// from a clean storage directory. <see cref="IAsyncDisposable.DisposeAsync"/> deletes
    /// ephemeral storage; reusable instances are preserved by design (see reaper docs).
    /// </summary>
    Task StopAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Executes a command and captures its exit code, stdout and stderr. Pass <paramref name="arguments"/>
    /// as <c>null</c> or omit it to run the command without arguments. <see cref="ExecOptions.Timeout"/>
    /// must be positive and at most 24 hours when set; per-command environment names must be valid
    /// Linux identifiers, with at most 1000 entries and values up to 128 KiB (UTF-8). At most 1000
    /// arguments may be passed.
    /// </summary>
    /// <param name="command">Executable or command name to run.</param>
    /// <param name="arguments">Command arguments, or <c>null</c> for none.</param>
    /// <param name="options">Exec settings, or <c>null</c> for container defaults.</param>
    /// <param name="cancellationToken">Cancels the command; the process is killed.</param>
    Task<ExecResult> ExecAsync(string command, string[]? arguments = null, ExecOptions? options = null, CancellationToken cancellationToken = default);

    /// <summary>
    /// Starts a long-running process inside the environment. The caller owns the returned
    /// <see cref="IWslProcess"/> and must dispose it; disposal terminates the process if it is
    /// still running. The container tracks live processes only with pruning of exited entries,
    /// so undisposed handles pin memory until container disposal. At most 1000 arguments may be
    /// passed.
    /// </summary>
    /// <param name="command">Executable or command name to run.</param>
    /// <param name="arguments">Command arguments, or <c>null</c> for none.</param>
    /// <param name="options">Process settings (working directory and per-process environment only), or <c>null</c> for container defaults.</param>
    /// <param name="cancellationToken">Cancels starting the process.</param>
    /// <exception cref="ArgumentException">
    /// <paramref name="options"/> is an <see cref="ExecOptions"/> carrying
    /// <see cref="ExecOptions.StandardInput"/> or <see cref="ExecOptions.Timeout"/>; those apply
    /// only to <see cref="ExecAsync"/>.
    /// </exception>
    IWslProcess StartProcess(string command, string[]? arguments = null, ProcessOptions? options = null, CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies a Windows file into the environment. Fails when <paramref name="hostPath"/> is missing
    /// or larger than 1 GiB. <paramref name="containerPath"/> must be an absolute Linux path.
    /// </summary>
    Task CopyToAsync(string hostPath, string containerPath, CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies a Linux file out of the environment to a Windows path. Fails when the container file
    /// exceeds 1 GiB. Parent directories of <paramref name="hostPath"/> are created.
    /// </summary>
    Task CopyFromAsync(string containerPath, string hostPath, CancellationToken cancellationToken = default);

    /// <summary>
    /// Subscribes to all logs captured by the environment. The stream is infinite until
    /// <paramref name="cancellationToken"/> fires or the container is disposed; use a
    /// <see cref="CancellationTokenSource"/> timeout or
    /// <see cref="Testing.LogDumper.DumpHeadAsync"/> with <c>maxLines</c> to bound the read.
    /// Abandoning the enumeration without cancellation/disposal pins a bounded subscriber buffer.
    /// </summary>
    IAsyncEnumerable<LogLine> SubscribeLogs(CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns a bounded snapshot of the most recent log lines, oldest first. Never blocks and
    /// never enumerates an infinite stream, so this is the right tool for failure triage.
    /// </summary>
    /// <param name="maxLines">Maximum number of trailing lines to return (must be positive).</param>
    IReadOnlyList<LogLine> GetRecentLogs(int maxLines = 50);
}
