namespace Wslc.Testcontainers.Networking;

/// <summary>
/// Exposes Linux service ports as Windows loopback ports. The implementation is deliberately
/// replaceable so that WSL networking changes do not leak into the public API.
/// </summary>
internal interface IWslNetwork : IAsyncDisposable
{
    /// <summary>Gets the Windows host address exposing the mapped ports.</summary>
    string Host { get; }

    /// <summary>Gets the Linux ports that currently have a host mapping.</summary>
    IReadOnlyCollection<int> Ports { get; }

    /// <summary>Gets the Windows loopback port mapped to a Linux port.</summary>
    int GetMappedPort(int containerPort);

    /// <summary>Probes whether a Linux port accepts TCP connections.</summary>
    Task<bool> IsPortOpenAsync(int containerPort, CancellationToken cancellationToken = default);
}
