using System.Net;
using System.Net.Sockets;

namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module containers implementing <see cref="IWslContainer"/>. Forwards
/// lifecycle, exec, copy and log operations so module users are not blocked when they need
/// ad-hoc initialization (e.g. seeding a database) without dropping to the inner container.
/// </summary>
public abstract class WslModuleContainer : IWslContainer
{
    private readonly IWslContainer _inner;

    /// <summary>Initializes a wrapper around the given container.</summary>
    protected WslModuleContainer(IWslContainer inner) => _inner = inner;

    /// <summary>Gets the unique name of the underlying WSLC instance.</summary>
    public string Name => _inner.Name;

    /// <summary>Gets the container image reference, when one was configured.</summary>
    public string? Image => _inner.Image;

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    public bool IsStarted => _inner.IsStarted;

    /// <inheritdoc />
    public bool IsReuseEffective => _inner.IsReuseEffective;

    /// <summary>
    /// Gets the Windows endpoint to connect to for a mapped Linux TCP port: the runtime-assigned
    /// host port and the port's configured bind address. The default/wildcard binding resolves to
    /// loopback: IPv4 <c>127.0.0.1</c> for <c>0.0.0.0</c> and IPv6 <c>::1</c> for <c>::</c>.
    /// </summary>
    public IPEndPoint GetConnectEndpoint(int containerPort) => _inner.GetConnectEndpoint(containerPort);

    /// <summary>Creates and provisions the environment.</summary>
    public Task StartAsync(CancellationToken cancellationToken = default) =>
        _inner.StartAsync(cancellationToken);

    /// <summary>Stops the environment and its processes (storage preserved for restart).</summary>
    public Task StopAsync(CancellationToken cancellationToken = default) =>
        _inner.StopAsync(cancellationToken);

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    public Task<ExecResult> ExecAsync(
        string command,
        string[]? arguments = null,
        ExecOptions? options = null,
        CancellationToken cancellationToken = default) =>
        _inner.ExecAsync(command, arguments, options, cancellationToken);

    /// <summary>Starts a long-running process inside the environment.</summary>
    public IWslProcess StartProcess(
        string command,
        string[]? arguments = null,
        ProcessOptions? options = null,
        CancellationToken cancellationToken = default) =>
        _inner.StartProcess(command, arguments, options, cancellationToken);

    /// <summary>Copies a Windows file into the environment.</summary>
    public Task CopyToAsync(string hostPath, string containerPath, CancellationToken cancellationToken = default) =>
        _inner.CopyToAsync(hostPath, containerPath, cancellationToken);

    /// <summary>Copies a Linux file out of the environment.</summary>
    public Task CopyFromAsync(string containerPath, string hostPath, CancellationToken cancellationToken = default) =>
        _inner.CopyFromAsync(containerPath, hostPath, cancellationToken);

    /// <summary>
    /// Subscribes to all logs captured by the environment. Infinite until <paramref name="cancellationToken"/>
    /// fires or the container is disposed; bound with <see cref="Testing.LogDumper.DumpHeadAsync"/>.
    /// </summary>
    public IAsyncEnumerable<LogLine> SubscribeLogs(CancellationToken cancellationToken = default) =>
        _inner.SubscribeLogs(cancellationToken);

    /// <summary>Returns a bounded snapshot of the most recent log lines, oldest first.</summary>
    public IReadOnlyList<LogLine> GetRecentLogs(int maxLines = 50) =>
        _inner.GetRecentLogs(maxLines);

    /// <summary>
    /// Disposes the wrapped container. Derived module containers may override this only when the
    /// module itself owns extra resources; by default it disposes the inner container. The base
    /// class is unsealed, so disposal suppresses finalization per CA1816.
    /// </summary>
    public virtual async ValueTask DisposeAsync()
    {
        GC.SuppressFinalize(this);
        await _inner.DisposeAsync().ConfigureAwait(false);
    }

    /// <summary>Brackets IPv6 literals so host:port stays a valid URL authority.</summary>
    protected static string FormatHost(IPAddress address) =>
        address.AddressFamily == AddressFamily.InterNetworkV6 ? $"[{address}]" : address.ToString();

    /// <summary>Renders an endpoint as host:port, e.g. <c>127.0.0.1:49153</c>.</summary>
    protected static string FormatEndpoint(IPEndPoint endpoint) => $"{FormatHost(endpoint.Address)}:{endpoint.Port}";

    /// <summary>Renders an HTTP base URL for the given endpoint, e.g. <c>http://127.0.0.1:49153</c>.</summary>
    protected static string FormatHttpEndpoint(IPEndPoint endpoint) => $"http://{FormatEndpoint(endpoint)}";

    /// <summary>Renders a connection string with host, port, username, password, and database.</summary>
    protected static string FormatConnectionString(IPEndPoint endpoint, string username, string password, string database) =>
        $"Host={endpoint.Address};Port={endpoint.Port};Username={username};Password={password};Database={database}";
}
