using System.Net;

namespace Wslc.Testcontainers.Waiting;

/// <summary>
/// The environment a wait strategy observes. Implementations are provided by
/// <see cref="WslContainer"/>; custom strategies can be written against this abstraction.
/// </summary>
/// <remarks>
/// A library-implemented service interface (per ADR-0008): consumers observe it but should not
/// implement it, because additive members would break external implementers.
/// </remarks>
public interface IWaitTarget
{
    /// <summary>Gets the WSLC instance name.</summary>
    string Name { get; }

    /// <summary>
    /// Gets the Windows endpoint to connect to for a mapped Linux TCP port: the runtime-assigned
    /// host port and the port's configured bind address. The default/wildcard binding resolves to
    /// loopback: IPv4 <c>127.0.0.1</c> for <c>0.0.0.0</c> and IPv6 <c>::1</c> for <c>::</c>.
    /// Before startup completes this throws; network probes run only while started.
    /// </summary>
    /// <exception cref="WslException">The container has not been started.</exception>
    /// <exception cref="WslNetworkException">The port was not declared with <c>WithPort</c> or the runtime has not assigned it.</exception>
    IPEndPoint GetConnectEndpoint(int containerPort);

    /// <summary>Executes a command inside the environment.</summary>
    Task<ExecResult> ExecAsync(string command, string[] arguments, CancellationToken cancellationToken);

    /// <summary>Probes a Linux TCP port.</summary>
    Task<bool> IsTcpPortOpenAsync(int containerPort, CancellationToken cancellationToken);

    /// <summary>Probes whether a Linux process is running.</summary>
    Task<bool> IsProcessRunningAsync(string processName, CancellationToken cancellationToken);

    /// <summary>Returns a snapshot of the recent logs.</summary>
    IReadOnlyList<LogLine> GetRecentLogs();
}
