namespace Wslc.Testcontainers.Waiting;

/// <summary>
/// The environment a wait strategy observes. Implementations are provided by
/// <see cref="WslContainer"/>; custom strategies can be written against this abstraction.
/// </summary>
public interface IWaitTarget
{
    /// <summary>Gets the WSLC instance name.</summary>
    string Name { get; }

    /// <summary>Gets the host exposing mapped ports.</summary>
    string Host { get; }

    /// <summary>Gets the mapped host port for a Linux port.</summary>
    int GetMappedPort(int containerPort);

    /// <summary>
    /// Gets the host address a probe should connect to for a mapped Linux TCP port. This is
    /// the mapping's bind address when one was configured, so readiness probes reach ports
    /// bound to a non-loopback Windows address; it falls back to <see cref="Host"/>.
    /// </summary>
    string GetProbeHost(int containerPort);

    /// <summary>Executes a command inside the environment.</summary>
    Task<ExecResult> ExecAsync(string command, string[] arguments, CancellationToken cancellationToken);

    /// <summary>Probes a Linux TCP port.</summary>
    Task<bool> IsTcpPortOpenAsync(int containerPort, CancellationToken cancellationToken);

    /// <summary>Probes whether a Linux process is running.</summary>
    Task<bool> IsProcessRunningAsync(string processName, CancellationToken cancellationToken);

    /// <summary>Returns a snapshot of the recent logs.</summary>
    IReadOnlyList<LogLine> GetRecentLogs();
}
