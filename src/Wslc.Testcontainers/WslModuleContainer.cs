namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module containers wrapping an underlying <see cref="IWslContainer"/>.
/// Forwards lifecycle, exec, copy and log operations so module users are not blocked when they
/// need ad-hoc initialization (e.g. seeding a database) without dropping to the inner container.
/// </summary>
public abstract class WslModuleContainer : IAsyncDisposable
{
    private readonly IWslContainer _inner;

    /// <summary>Initializes a wrapper around the given container.</summary>
    protected WslModuleContainer(IWslContainer inner) => _inner = inner;

    /// <summary>Gets the underlying container for advanced scenarios.</summary>
    public IWslContainer Inner => _inner;

    /// <summary>Gets the unique name of the underlying WSLC instance.</summary>
    public string Name => _inner.Name;

    /// <summary>Gets the container image reference, when one was configured.</summary>
    public string? Image => _inner.Image;

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    public bool IsStarted => _inner.IsStarted;

    /// <summary>Gets the host address that exposes mapped ports.</summary>
    public string Host => _inner.Host;

    /// <summary>Creates and provisions the environment.</summary>
    public Task StartAsync(CancellationToken cancellationToken = default) =>
        _inner.StartAsync(cancellationToken);

    /// <summary>Stops the environment and its processes (storage preserved for restart).</summary>
    public Task StopAsync(CancellationToken cancellationToken = default) =>
        _inner.StopAsync(cancellationToken);

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    public Task<ExecResult> ExecAsync(string command, params string[] arguments) =>
        _inner.ExecAsync(command, arguments);

    /// <summary>Executes a command with additional options (pass <c>null</c> for defaults).</summary>
    public Task<ExecResult> ExecAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default) =>
        _inner.ExecAsync(command, arguments, options, cancellationToken);

    /// <summary>Starts a long-running process inside the environment.</summary>
    public IWslProcess StartProcess(string command, params string[] arguments) =>
        _inner.StartProcess(command, arguments);

    /// <summary>Starts a long-running process with additional options.</summary>
    public IWslProcess StartProcess(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default) =>
        _inner.StartProcess(command, arguments, options, cancellationToken);

    /// <summary>Starts a long-running process inside the environment.</summary>
    [Obsolete("Use StartProcess(...) instead.")]
    public IWslProcess StartProcessAsync(string command, params string[] arguments) =>
        _inner.StartProcess(command, arguments);

    /// <summary>Starts a long-running process with additional options.</summary>
    [Obsolete("Use StartProcess(...) instead.")]
    public IWslProcess StartProcessAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default) =>
        _inner.StartProcess(command, arguments, options, cancellationToken);

    /// <summary>Copies a Windows file into the environment.</summary>
    public Task CopyToAsync(string hostPath, string containerPath, CancellationToken cancellationToken = default) =>
        _inner.CopyToAsync(hostPath, containerPath, cancellationToken);

    /// <summary>Copies a Linux file out of the environment.</summary>
    public Task CopyFromAsync(string containerPath, string hostPath, CancellationToken cancellationToken = default) =>
        _inner.CopyFromAsync(containerPath, hostPath, cancellationToken);

    /// <summary>
    /// Streams all logs captured by the environment. Infinite until <paramref name="cancellationToken"/>
    /// fires or the container is disposed; bound with <see cref="Testing.LogDumper.DumpAsync"/>.
    /// </summary>
    public IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default) =>
        _inner.LogsAsync(cancellationToken);

    /// <inheritdoc />
    public ValueTask DisposeAsync() => _inner.DisposeAsync();

    /// <summary>Gets the Windows loopback port mapped to a Linux service port.</summary>
    public int GetMappedPort(int containerPort) => _inner.GetMappedPort(containerPort);
}
