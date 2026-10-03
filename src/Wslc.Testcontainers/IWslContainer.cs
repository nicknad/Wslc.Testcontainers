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
    /// Gets the Windows loopback address (<c>127.0.0.1</c>) for mapped ports. Ports bound
    /// to a different address (see <c>WithPort(port, bindAddress)</c>) must be
    /// reached via that address instead.
    /// </summary>
    string Host { get; }

    /// <summary>Gets the Windows port mapped to a Linux TCP service port.</summary>
    /// <exception cref="WslcException">The container has not been started.</exception>
    /// <exception cref="WslNetworkException">The port was not declared with <c>WithPort</c> or the runtime has not assigned it.</exception>
    int GetMappedPort(int port);

    /// <summary>
    /// Gets the Windows address to connect to for a mapped Linux TCP port: the port's configured
    /// bind address, or <see cref="Host"/> (loopback) for the default/wildcard binding. Before
    /// <see cref="StartAsync"/> this returns <see cref="Host"/>.
    /// </summary>
    string GetMappedHost(int port);

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

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    Task<ExecResult> ExecAsync(string command, params string[] arguments);

    /// <summary>
    /// Executes a command with additional options. Pass <c>null</c> for <paramref name="options"/>
    /// to use container defaults. <see cref="ExecOptions.Timeout"/> must be positive when set;
    /// per-command environment names must be valid Linux identifiers.
    /// </summary>
    Task<ExecResult> ExecAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default);

    /// <summary>
    /// Executes a command with additional options and no arguments. Pass <c>null</c> for
    /// <paramref name="options"/> to use container defaults; the cancellation token is required
    /// so the no-argument case stays cancellable.
    /// </summary>
    Task<ExecResult> ExecAsync(string command, ExecOptions? options, CancellationToken cancellationToken);

    /// <summary>
    /// Starts a long-running process inside the environment. The caller owns the returned
    /// <see cref="IWslProcess"/> and must dispose it; disposal terminates the process if it is
    /// still running. The container tracks live processes only with pruning of exited entries,
    /// so undisposed handles pin memory until container disposal.
    /// </summary>
    /// <remarks>
    /// <see cref="ExecOptions.StandardInput"/> and <see cref="ExecOptions.Timeout"/> apply only to
    /// <c>ExecAsync</c> and must be
    /// <c>null</c> here; passing them throws <see cref="ArgumentException"/>.
    /// </remarks>
    IWslProcess StartProcess(string command, params string[] arguments);

    /// <summary>
    /// Starts a long-running process with additional options (working directory and per-process
    /// environment only; <c>StandardInput</c>/<c>Timeout</c> are rejected).
    /// </summary>
    IWslProcess StartProcess(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default);

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
    /// Streams all logs captured by the environment. The stream is infinite until
    /// <paramref name="cancellationToken"/> fires or the container is disposed; use a
    /// <see cref="CancellationTokenSource"/> timeout or
    /// <see cref="Testing.LogDumper.DumpAsync"/> with <c>maxLines</c> to bound the read.
    /// Abandoning the enumeration without cancellation/disposal pins a bounded subscriber buffer.
    /// </summary>
    IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns a bounded snapshot of the most recent log lines, oldest first. Never blocks and
    /// never enumerates an infinite stream, so this is the right tool for failure triage.
    /// </summary>
    /// <param name="maxLines">Maximum number of trailing lines to return (must be positive).</param>
    IReadOnlyList<LogLine> GetRecentLogs(int maxLines = 50);
}
