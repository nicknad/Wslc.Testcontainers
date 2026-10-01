namespace Wslc.Testcontainers;

/// <summary>
/// A disposable, isolated WSL container managed by WSLC on top of the official
/// <c>Microsoft.WSL.Containers</c> API.
/// </summary>
public interface IWslContainer : IAsyncDisposable
{
    /// <summary>Gets the unique name of the underlying WSLC instance.</summary>
    string Name { get; }

    /// <summary>Gets the container image reference, when one was configured.</summary>
    string? Image { get; }

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    bool IsStarted { get; }

    /// <summary>Gets the host address that exposes mapped ports (always a Windows loopback address).</summary>
    string Host { get; }

    /// <summary>Gets the Windows loopback port mapped to a Linux service port.</summary>
    int GetMappedPort(int port);

    /// <summary>Creates and provisions the environment, then waits until all readiness strategies pass.</summary>
    Task StartAsync(CancellationToken cancellationToken = default);

    /// <summary>Stops all processes and terminates the WSLC session without deleting stored state.</summary>
    Task StopAsync(CancellationToken cancellationToken = default);

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    Task<ExecResult> ExecAsync(string command, params string[] arguments);

    /// <summary>Executes a command with additional options.</summary>
    Task<ExecResult> ExecAsync(string command, string[] arguments, ExecOptions options, CancellationToken cancellationToken = default);

    /// <summary>Starts a long-running process inside the environment.</summary>
    IWslProcess StartProcessAsync(string command, params string[] arguments);

    /// <summary>Starts a long-running process with additional options.</summary>
    IWslProcess StartProcessAsync(string command, string[] arguments, ExecOptions options, CancellationToken cancellationToken = default);

    /// <summary>Copies a Windows file into the environment.</summary>
    Task CopyToAsync(string source, string destination, CancellationToken cancellationToken = default);

    /// <summary>Copies a Linux file out of the environment.</summary>
    Task CopyFromAsync(string source, string destination, CancellationToken cancellationToken = default);

    /// <summary>Streams all logs captured by the environment.</summary>
    IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default);

    /// <summary>Streams the standard output captured by the environment.</summary>
    IAsyncEnumerable<string> Stdout { get; }

    /// <summary>Streams the standard error captured by the environment.</summary>
    IAsyncEnumerable<string> Stderr { get; }
}
