namespace Wslc.Testcontainers;

/// <summary>
/// A long-running process started inside a WSLC environment. The caller owns the handle:
/// disposing it terminates a still-running process, so <c>DisposeAsync</c> both releases the
/// handle and cleans the process up.
/// </summary>
public interface IWslProcess : IAsyncDisposable
{
    /// <summary>Gets the Linux process id when it could be resolved.</summary>
    int? Id { get; }

    /// <summary>Gets a value indicating whether the process has exited.</summary>
    bool HasExited { get; }

    /// <summary>Gets the process exit code. Throws when the process has not exited.</summary>
    int ExitCode { get; }

    /// <summary>Waits until the process exits and returns its exit code.</summary>
    Task<int> WaitForExitAsync(CancellationToken cancellationToken = default);

    /// <summary>Terminates the process (SIGTERM, then SIGKILL after a grace period).</summary>
    Task KillAsync(CancellationToken cancellationToken = default);
}
