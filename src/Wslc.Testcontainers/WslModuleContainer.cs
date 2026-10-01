namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module containers wrapping an underlying <see cref="IWslContainer"/>.
/// </summary>
public abstract class WslModuleContainer : IAsyncDisposable
{
    private readonly IWslContainer _inner;

    /// <summary>Initializes a wrapper around the given container.</summary>
    protected WslModuleContainer(IWslContainer inner) => _inner = inner;

    /// <summary>Gets the unique name of the underlying WSLC instance.</summary>
    public string Name => _inner.Name;

    /// <summary>Gets the host address that exposes mapped ports.</summary>
    public string Host => _inner.Host;

    /// <summary>Creates and provisions the environment.</summary>
    public Task StartAsync(CancellationToken cancellationToken = default) =>
        _inner.StartAsync(cancellationToken);

    /// <summary>Stops the environment and its processes.</summary>
    public Task StopAsync(CancellationToken cancellationToken = default) =>
        _inner.StopAsync(cancellationToken);

    /// <summary>Streams all logs captured by the environment.</summary>
    public IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default) =>
        _inner.LogsAsync(cancellationToken);

    /// <inheritdoc />
    public ValueTask DisposeAsync()
    {
        GC.SuppressFinalize(this);
        return _inner.DisposeAsync();
    }

    /// <summary>Gets the Windows loopback port mapped to a Linux service port.</summary>
    protected int GetMappedPort(int containerPort) => _inner.GetMappedPort(containerPort);
}
