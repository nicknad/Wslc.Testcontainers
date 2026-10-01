using System.Globalization;
using System.Net;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Internal;
using Wslc.Testcontainers.Networking;
using Wslc.Testcontainers.Provisioning;
using Wslc.Testcontainers.Runtime;
using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// A disposable, isolated WSL container managed by WSLC on top of the official
/// <see cref="Microsoft.WSL.Containers"/> API. Create instances through
/// <see cref="WslContainerBuilder"/>, then call <see cref="StartAsync"/>.
/// </summary>
/// <remarks>
/// Lifecycle transitions (<see cref="StartAsync"/>, <see cref="StopAsync"/>, <see cref="DisposeAsync"/>)
/// are serialized. Commands, copies and long-running processes are intentionally not gated:
/// starting one while the container stops aborts that operation with an exception instead of
/// corrupting state. Don't expect operations issued concurrently with <see cref="DisposeAsync"/>
/// to succeed.
/// </remarks>
public sealed class WslContainer : IWslContainer, IWaitTarget
{
    private readonly WslContainerConfiguration _configuration;
    private readonly WslInstanceStore _store;
    private readonly LogBroadcaster _logs = new();
    private readonly SemaphoreSlim _lifecycle = new(1, 1);
    private readonly ProcessRegistry _processes = new();
    private readonly bool _reuse;
    private readonly string _ownerProcessId;
    private readonly Lazy<string> _name;

    // Mutable state is published with volatile so lock-free readers (Exec/Copy/GetMappedPort
    // and the process-exit hook) observe a consistent reference instead of a torn read.
    private volatile Session? _session;
    private volatile Microsoft.WSL.Containers.Container? _container;
    private volatile WslcPortMapping? _network;
    private volatile ContainerProcess? _mainProcess;
    private volatile WslInstanceMetadata? _metadata;
    private volatile bool _storageCreated;
    private volatile bool _started;
    private volatile bool _disposed;
    private int _disposeRequested;
    private volatile FileStream? _reuseLock;

    internal WslContainer(WslContainerConfiguration configuration)
        : this(configuration, WslInstanceStore.Default)
    {
    }

    internal WslContainer(WslContainerConfiguration configuration, WslInstanceStore store)
    {
        _configuration = configuration;
        _store = store;
        _reuse = (configuration.Reuse ?? WslcEnvironment.ReuseByDefault) && WslcEnvironment.ReuseAllowed;

        // Lazy so constructing a reuse container does not synchronously SHA256 every WithFile
        // source until the name is actually needed (start, metadata, diagnostics).
        _name = new Lazy<string>(() => _reuse
            ? WslNaming.CreateReuseName(WslConfigHasher.Compute(configuration))
            : WslNaming.CreateInstanceName(store.SessionId));
        _ownerProcessId = Environment.ProcessId.ToString(CultureInfo.InvariantCulture);
    }

    /// <inheritdoc />
    public string Name => _name.Value;

    /// <inheritdoc />
    public string Host => IPAddress.Loopback.ToString();

    /// <summary>Gets the container image reference, when one was configured.</summary>
    public string? Image => _configuration.Image ?? _configuration.TarballImageName;

    /// <summary>Gets a value indicating whether the container is started.</summary>
    public bool IsStarted => _started;

    internal WslContainerConfiguration Configuration => _configuration;

    /// <inheritdoc />
    public int GetMappedPort(int port)
    {
        var network = _network
            ?? throw new WslNetworkException($"Container '{Name}' has not been started, so port {port} is not mapped yet.");
        return network.GetMappedPort(port);
    }

    /// <inheritdoc />
    public Task StartAsync(CancellationToken cancellationToken = default)
    {
        WslPlatform.ThrowIfUnsupported();
        return RunExclusiveAsync(async () =>
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (!_started)
            {
                await StartCoreAsync(cancellationToken).ConfigureAwait(false);
            }
        }, cancellationToken);
    }

    /// <inheritdoc />
    public Task StopAsync(CancellationToken cancellationToken = default) =>
        RunExclusiveAsync(async () =>
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            await StopCoreAsync(cancellationToken, throwOnError: true).ConfigureAwait(false);
        }, cancellationToken);

    /// <inheritdoc />
    public Task<ExecResult> ExecAsync(string command, params string[] arguments) =>
        ExecInternalAsync(command, arguments, null, CancellationToken.None);

    /// <inheritdoc />
    public Task<ExecResult> ExecAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default) =>
        ExecInternalAsync(command, arguments, options, cancellationToken);

    /// <inheritdoc />
    public IWslProcess StartProcess(string command, params string[] arguments) =>
        StartProcess(command, arguments, null, CancellationToken.None);

    /// <inheritdoc />
    public IWslProcess StartProcess(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ArgumentException.ThrowIfNullOrWhiteSpace(command);
        ArgumentNullException.ThrowIfNull(arguments);
        if (options?.StandardInput is not null)
        {
            throw new ArgumentException("StandardInput applies only to ExecAsync, not to long-running StartProcess. Use ExecAsync for stdin.", nameof(options));
        }

        if (options?.Timeout is not null)
        {
            throw new ArgumentException("Timeout applies only to ExecAsync, not to long-running StartProcess. Kill the IWslProcess when done.", nameof(options));
        }

        ValidateExecOptions(options);
        var container = RequireContainer();

        var settings = BuildProcessSettings(command, arguments, options, enableStandardInput: false);

        // Register before starting: a concurrent Stop/Dispose snapshots this list and would
        // otherwise miss (and leak) a process that was started but not yet added.
        var process = WslcProcessRunner.Create(container, settings, _logs.Publish);
        _processes.Add(process);
        try
        {
            process.Start();
        }
        catch
        {
            _processes.Remove(process);
            process.DisposeAsync().AsTask().GetAwaiter().GetResult();
            throw;
        }

        return process;
    }

    /// <inheritdoc />
    [Obsolete("Use StartProcess(...) instead. StartProcessAsync was misnamed: it starts synchronously and returns IWslProcess, not Task.")]
    public IWslProcess StartProcessAsync(string command, params string[] arguments) =>
        StartProcess(command, arguments, null, CancellationToken.None);

    /// <inheritdoc />
    [Obsolete("Use StartProcess(...) instead. StartProcessAsync was misnamed: it starts synchronously and returns IWslProcess, not Task.")]
    public IWslProcess StartProcessAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken = default) =>
        StartProcess(command, arguments, options, cancellationToken);

    /// <inheritdoc />
    public Task CopyToAsync(string hostPath, string containerPath, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(hostPath);
        RequireContainerPath(containerPath, nameof(containerPath));
        var container = RequireContainer();
        var fullHostPath = Path.GetFullPath(hostPath);
        if (!File.Exists(fullHostPath))
        {
            throw new WslcException($"Host file '{hostPath}' does not exist. Only file copies are supported.");
        }

        const long MaxCopyBytes = 1024L * 1024L * 1024L;
        var length = new FileInfo(fullHostPath).Length;
        if (length > MaxCopyBytes)
        {
            throw new WslcException($"Copying '{hostPath}' to '{containerPath}' exceeds 1 GiB limit ({length} bytes).");
        }

        return WslcProcessRunner.CopyToAsync(container, fullHostPath, containerPath, cancellationToken, _logs.Publish);
    }

    /// <inheritdoc />
    public Task CopyFromAsync(string containerPath, string hostPath, CancellationToken cancellationToken = default)
    {
        RequireContainerPath(containerPath, nameof(containerPath));
        ArgumentException.ThrowIfNullOrWhiteSpace(hostPath);
        var container = RequireContainer();
        return WslcProcessRunner.CopyFromAsync(container, containerPath, hostPath, cancellationToken, _logs.Publish);
    }

    /// <inheritdoc />
    public IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default) =>
        _logs.StreamAsync(cancellationToken);

    Task<ExecResult> IWaitTarget.ExecAsync(string command, string[] arguments, CancellationToken cancellationToken) =>
        ExecInternalAsync(command, arguments, null, cancellationToken);

    Task<bool> IWaitTarget.IsTcpPortOpenAsync(int containerPort, CancellationToken cancellationToken) =>
        _network is { } network
            ? network.IsPortOpenAsync(containerPort, cancellationToken)
            : Task.FromResult(false);

    Task<bool> IWaitTarget.IsProcessRunningAsync(string processName, CancellationToken cancellationToken) =>
        IsProcessRunningAsync(processName, cancellationToken);

    IReadOnlyList<LogLine> IWaitTarget.GetRecentLogs() => _logs.Snapshot();

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposeRequested, 1) != 0)
        {
            // Cleanup already ran (or is running) from another path, e.g. the process-exit
            // hook. Completing is idempotent and still terminates LogsAsync consumers.
            _logs.Complete();
            return;
        }

        _disposed = true;
        try
        {
            await RunExclusiveAsync(() => CleanupAsync(throwOnError: false, CancellationToken.None)).ConfigureAwait(false);
        }
        finally
        {
            _logs.Complete();
        }

        // Deliberately not disposing _lifecycle: a caller may be queued on WaitAsync, and
        // disposing the semaphore under it would replace the container's ObjectDisposedException
        // with a SemaphoreSlim one (or fault Release). SemaphoreSlim holds no unmanaged
        // resources unless AvailableWaitHandle is used, which this type never does.
    }

    private async Task RunExclusiveAsync(Func<Task> operation, CancellationToken cancellationToken = default)
    {
        await _lifecycle.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await operation().ConfigureAwait(false);
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    internal async Task<ExecResult> ExecInternalAsync(string command, string[] arguments, ExecOptions? options, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(command);
        ArgumentNullException.ThrowIfNull(arguments);
        ValidateExecOptions(options);

        var container = RequireContainer();
        options ??= new ExecOptions();

        var settings = BuildProcessSettings(
            command,
            arguments,
            options,
            enableStandardInput: options.StandardInput is not null);

        try
        {
            return await WslcProcessRunner
                .RunAsync(container, settings, options.StandardInput, options.Timeout, cancellationToken, _logs.Publish)
                .ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is not WslcException and not OperationCanceledException)
        {
            throw new WslProcessException($"Command '{command}' failed: {exception.Message}", exception);
        }
    }

    internal async Task<bool> IsProcessRunningAsync(string processName, CancellationToken cancellationToken)
    {
        var result = await ExecInternalAsync(
            "sh",
            new[]
            {
                "-c",
                "for p in /proc/[0-9]*; do [ \"$(cat \"$p/comm\" 2>/dev/null)\" = \"$1\" ] && exit 0; done; exit 1",
                "sh",
                processName,
            },
            null,
            cancellationToken).ConfigureAwait(false);

        return result.ExitCode == 0;
    }

    /// <summary>Best-effort synchronous cleanup used from process-exit handlers.</summary>
    internal void CleanupSynchronously()
    {
        // The exit hook can race an in-flight DisposeAsync; only the first caller tears down.
        if (Interlocked.Exchange(ref _disposeRequested, 1) != 0)
        {
            return;
        }

        _disposed = true;
        _started = false;

        if (_mainProcess is { HasExited: false })
        {
            var main = _mainProcess;
            Ignore(() => main.NativeProcess.Signal(Signal.SIGKILL));
        }

        var container = _container;
        if (container is not null)
        {
            Ignore(() => container.Stop(Signal.SIGTERM, TimeSpan.FromSeconds(2)));
            Ignore(() => container.Delete(DeleteContainerOption.Force));
        }

        var session = _session;
        if (session is not null)
        {
            Ignore(session.Terminate);
            Ignore(session.Dispose);
        }

        UpdateState("Stopped");
        Ignore(ReleaseReuseLock);

        if (!_reuse && _storageCreated)
        {
            WslInstanceStore.BestEffortDeleteDirectory(_store.GetInstanceDirectory(Name));
        }

        WslContainerHost.Unregister(Name);
        _logs.Complete();
    }

    private static void Ignore(Action action)
    {
        try
        {
            action();
        }
        catch
        {
            // Best-effort only; intentionally ignored.
        }
    }

    private async Task StartCoreAsync(CancellationToken cancellationToken)
    {
        using var startupTimeout = new CancellationTokenSource(_configuration.StartupTimeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, startupTimeout.Token);
        var token = linked.Token;

        try
        {
            await WslContainerHost.EnsureInitializedAsync(token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();

            if (_configuration.Reuse == true && !_reuse)
            {
                _logs.Publish(LogLine.Diagnostic("reuse was requested but is disabled (default off, or blocked under CI without WSLC_REUSE_IN_CI); using ephemeral instance"));
            }

            _logs.Publish(LogLine.Diagnostic($"creating WSL container '{Name}' (runtime {WslcHost.GetVersion()})"));
            var image = await CreateSessionAndContainerAsync(token).ConfigureAwait(false);

            await CopyConfiguredFilesAsync(token).ConfigureAwait(false);
            await WaitForReadinessAsync(token).ConfigureAwait(false);

            _started = true;
            UpdateState("Running");
            WslContainerHost.Register(this);
            _logs.Publish(LogLine.Diagnostic($"container '{Name}' is ready (image '{image}')"));
        }
        catch (Exception exception)
        {
            _logs.Publish(LogLine.Diagnostic($"startup failed: {exception.Message}"));
            await CleanupAsync(throwOnError: false, CancellationToken.None).ConfigureAwait(false);
            throw Enrich(Translate(exception, startupTimeout, cancellationToken));
        }
    }

    private async Task<string> CreateSessionAndContainerAsync(CancellationToken cancellationToken)
    {
        await AcquireReuseLockAsync(cancellationToken).ConfigureAwait(false);
        var storagePath = EnsureStorageAndMetadata();
        StartSession(storagePath);

        var resolver = new WslImageResolver(_session!, _configuration, _logs.Publish);
        var image = await resolver.ResolveAsync($"{Name}:local", cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        UpdateImageMetadata(image);

        CreateAndStartContainer(image);
        await ResolveMappedPortsIfNeededAsync(cancellationToken).ConfigureAwait(false);

        return image;
    }

    private string EnsureStorageAndMetadata()
    {
        var storagePath = _store.GetSessionStorageDirectory(Name);
        // The runtime requires an empty storage directory. StopAsync preserves the instance
        // directory (for restart by name), so reset storage here to make Start-after-Stop work
        // instead of failing with an obscure "directory not empty" runtime error.
        if (Directory.Exists(storagePath))
        {
            WslInstanceStore.BestEffortDeleteDirectory(storagePath);
        }

        Directory.CreateDirectory(storagePath);
        _storageCreated = true;

        _metadata = new WslInstanceMetadata(
            _store.SessionId,
            Name,
            Environment.ProcessId,
            DateTimeOffset.UtcNow)
        {
            State = "Creating",
            Owner = Environment.UserName,
            Image = _configuration.Image,
            Reuse = _reuse,
        };
        _store.WriteMetadata(_metadata);
        return storagePath;
    }

    private async Task AcquireReuseLockAsync(CancellationToken cancellationToken)
    {
        if (!_reuse)
        {
            return;
        }

        var instanceDirectory = _store.GetInstanceDirectory(Name);
        Directory.CreateDirectory(instanceDirectory);
        var lockPath = Path.Combine(instanceDirectory, "wslc.lock");

        // Per ADR bounded loops we don't allow while(true): a reuse instance is shared by
        // configuration across processes, so serialize owners with a fixed retry budget.
        // 100 attempts cover 30s @ 300ms, capped earlier by the startup timeout token.
        const int MaxAttempts = 100;
        for (var attempt = 0; attempt < MaxAttempts; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                _reuseLock = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                return;
            }
            catch (IOException)
            {
                // Another process owns the reuse instance; retry until the budget is spent.
            }

            await Task.Delay(300, cancellationToken).ConfigureAwait(false);
        }

        throw new WslProvisioningException(
            $"Reuse instance '{Name}' is locked by another process and was not released within 30s.");
    }

    private void ReleaseReuseLock()
    {
        var reuseLock = _reuseLock;
        _reuseLock = null;
        reuseLock?.Dispose();
    }

    private void StartSession(string storagePath)
    {
        try
        {
            _session = new Session(new SessionSettings(Name, storagePath));
        }
        catch (Exception exception)
        {
            throw WslRuntimeException.FromHResult($"Failed to create WSL container session '{Name}'.", exception);
        }

        _session.Terminated += OnSessionTerminated;

        try
        {
            _session.Start();
        }
        catch (Exception exception)
        {
            throw WslRuntimeException.FromHResult($"Failed to start WSL container session '{Name}'.", exception);
        }
    }

    private void UpdateImageMetadata(string image)
    {
        if (_metadata is not null)
        {
            // In-memory only: the running/stopped state transition persists this with the
            // next write, saving one metadata write per start.
            _metadata = _metadata with { Image = image };
        }
    }

    private void CreateAndStartContainer(string image)
    {
        _network = WslcPortMapping.Create(_configuration.Ports);
        var settings = BuildContainerSettings(image);

        try
        {
            _container = _session!.CreateContainer(settings);
        }
        catch (Exception exception)
        {
            throw new WslProvisioningException($"Failed to create the container from image '{image}': {exception.Message}", exception);
        }

        _mainProcess = new ContainerProcess(_container.InitProcess, captureOutput: true, _logs.Publish);

        try
        {
            _container.Start();
        }
        catch (Exception exception)
        {
            throw new WslProvisioningException($"Failed to start the container from image '{image}': {exception.Message}", exception);
        }

        _logs.Publish(LogLine.Diagnostic($"container started (id {_container.Id})"));
    }

    private ContainerSettings BuildContainerSettings(string image)
    {
        var settings = new ContainerSettings(image)
        {
            Name = Name,
            InitProcess = BuildInitProcessSettings(),
            EnableAutoRemove = false,
            NetworkingMode = ContainerNetworkingMode.Bridged,
        };

        foreach (var mapping in _network!.ToContainerPortMappings())
        {
            settings.PortMappings.Add(mapping);
        }

        foreach (var volume in _configuration.Volumes)
        {
            settings.Volumes.Add(new ContainerVolume(volume.HostPath, volume.ContainerPath, volume.ReadOnly));
        }

        return settings;
    }

    private async Task ResolveMappedPortsIfNeededAsync(CancellationToken cancellationToken)
    {
        if (_network!.Ports.Count == 0)
        {
            return;
        }

        await ResolveMappedPortsAsync(cancellationToken).ConfigureAwait(false);
        _logs.Publish(LogLine.Diagnostic($"mapped ports: {FormatMappedPorts()}"));
    }

    private string FormatMappedPorts() =>
        string.Join(", ", _configuration.Ports.Select(port => $"{port}->{_network!.GetMappedPort(port)}"));

    private async Task ResolveMappedPortsAsync(CancellationToken cancellationToken)
    {
        // Per ADR bounded loops we don't allow while(true): poll the inspect payload
        // with a fixed attempt count. 200 attempts cover 10s @ 50ms delay.
        const int MaxAttempts = 200;
        for (var attempt = 0; attempt < MaxAttempts; attempt++)
        {
            _network!.ResolveFromInspect(_container!.Inspect());
            if (_network.UnresolvedCount == 0)
            {
                return;
            }

            await Task.Delay(50, cancellationToken).ConfigureAwait(false);
        }

        throw new WslNetworkException(
            $"The WSL runtime did not assign host ports for container port(s): {string.Join(", ", _network!.UnresolvedPorts)}.");
    }

    private ProcessSettings BuildInitProcessSettings()
    {
        List<string> commandLine;
        if (_configuration.Command is { } command)
        {
            commandLine = BuildCommandLine(command, _configuration.CommandArguments);
        }
        else
        {
            // Keep the environment alive so commands can be executed against it.
            commandLine = new List<string>(3) { "/bin/sh", "-c", "while true; do sleep 3600; done" };
        }

        return WslcProcessRunner.CreateSettings(
            commandLine,
            _configuration.WorkingDirectory,
            BuildEnvironment(null));
    }

    private async Task CopyConfiguredFilesAsync(CancellationToken cancellationToken)
    {
        foreach (var file in _configuration.Files)
        {
            _logs.Publish(LogLine.Diagnostic($"copying '{file.Source}' to '{file.Destination}'"));
            await WslcProcessRunner
                .CopyToAsync(_container!, file.Source, file.Destination, cancellationToken, _logs.Publish)
                .ConfigureAwait(false);
        }
    }

    private async Task WaitForReadinessAsync(CancellationToken cancellationToken)
    {
        if (_configuration.WaitStrategies.Count == 0)
        {
            return;
        }

        IWaitTarget target = this;
        foreach (var strategy in _configuration.WaitStrategies)
        {
            _logs.Publish(LogLine.Diagnostic($"waiting for {strategy.Name} (timeout {strategy.Timeout.TotalSeconds:0.###}s)"));
            await strategy.WaitAsync(target, cancellationToken).ConfigureAwait(false);
        }
    }

    private Dictionary<string, string> BuildEnvironment(IReadOnlyDictionary<string, string>? overrides)
    {
        if (overrides is not null)
        {
            foreach (var key in overrides.Keys)
            {
                ValidateEnvironmentName(key);
            }

            foreach (var pair in overrides)
            {
                if (pair.Value is null)
                {
                    throw new ArgumentException($"Environment variable '{pair.Key}' has null value.", nameof(overrides));
                }
            }
        }

        var baseEnv = _configuration.Environment;
        var environment = new Dictionary<string, string>(
            baseEnv.Count + 4 + (overrides?.Count ?? 0),
            StringComparer.Ordinal);
        foreach (var pair in baseEnv)
        {
            environment[pair.Key] = pair.Value;
        }

        environment["WSLC_SESSION_ID"] = _store.SessionId;
        environment["WSLC_INSTANCE_ID"] = Name;
        environment["WSLC_OWNER_PID"] = _ownerProcessId;
        environment["WSLC_CREATED_AT"] = (_metadata?.CreatedAt ?? DateTimeOffset.UtcNow).ToString("O", CultureInfo.InvariantCulture);

        if (overrides is not null)
        {
            foreach (var pair in overrides)
            {
                environment[pair.Key] = pair.Value;
            }
        }

        return environment;
    }

    private ProcessSettings BuildProcessSettings(string command, IReadOnlyList<string> arguments, ExecOptions? options, bool enableStandardInput)
    {
        var commandLine = BuildCommandLine(command, arguments);
        return WslcProcessRunner.CreateSettings(
            commandLine,
            options?.WorkingDirectory ?? _configuration.WorkingDirectory,
            BuildEnvironment(options?.Environment),
            enableStandardInput: enableStandardInput);
    }

    private static List<string> BuildCommandLine(string command, IReadOnlyList<string> arguments)
    {
        var commandLine = new List<string>(arguments.Count + 1) { command };
        commandLine.AddRange(arguments);
        return commandLine;
    }

    private Microsoft.WSL.Containers.Container RequireContainer() =>
        _container ?? throw new InvalidOperationException($"Container '{Name}' has not been started. Call StartAsync() first.");

    private async Task CleanupAsync(bool throwOnError, CancellationToken cancellationToken)
    {
        Exception? failure = null;

        try
        {
            await StopCoreAsync(cancellationToken, throwOnError: false).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failure = exception;
        }

        ReleaseReuseLock();

        if (!_reuse && _storageCreated)
        {
            _store.DeleteInstanceDirectory(Name);
            _storageCreated = false;
        }

        if (failure is not null && throwOnError)
        {
            throw new WslCleanupException($"Failed to clean up container '{Name}'.", failure);
        }
    }

    private async Task StopCoreAsync(CancellationToken cancellationToken, bool throwOnError)
    {
        var failures = new List<Exception>(4);

        foreach (var process in _processes.Snapshot())
        {
            try
            {
                await process.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        _processes.Clear();

        await DisposeMainProcessAsync(failures).ConfigureAwait(false);
        StopAndDeleteContainer(failures);
        TerminateSession(failures);

        _network = null;
        _started = false;
        UpdateState("Stopped");
        WslContainerHost.Unregister(Name);

        if (failures.Count > 0 && throwOnError)
        {
            throw new WslCleanupException($"Failed to stop container '{Name}'.", failures[0]);
        }
    }

    private async Task DisposeMainProcessAsync(List<Exception> failures)
    {
        if (_mainProcess is null)
        {
            return;
        }

        try
        {
            await _mainProcess.DisposeAsync().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }

        _mainProcess = null;
    }

    private void StopAndDeleteContainer(List<Exception> failures)
    {
        if (_container is null)
        {
            return;
        }

        TryStep(failures, () => _container.Stop(Signal.SIGTERM, TimeSpan.FromSeconds(10)), IsBenignRuntimeError);
        TryStep(failures, () => _container.Delete(DeleteContainerOption.Force), IsBenignRuntimeError);
        _container = null;
    }

    private void TerminateSession(List<Exception> failures)
    {
        if (_session is null)
        {
            return;
        }

        Ignore(() => _session.Terminated -= OnSessionTerminated);
        TryStep(failures, _session.Terminate, IsBenignRuntimeError);
        try
        {
            _session.Dispose();
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }

        _session = null;
    }

    private static void TryStep(List<Exception> failures, Action action, Func<Exception, bool> isBenign)
    {
        try
        {
            action();
        }
        catch (Exception exception) when (isBenign(exception))
        {
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }
    }

    private Exception Enrich(Exception exception)
    {
        if (exception is not WslReadinessException readiness)
        {
            return exception;
        }

        readiness.Image ??= _configuration.Image ?? _configuration.TarballPath;
        readiness.Command ??= _configuration.Command;

        if (_mainProcess is { HasExited: true } mainProcess)
        {
            readiness.ExitCode ??= mainProcess.ExitCode;
        }

        var logs = _logs.Snapshot();
        readiness.Stdout ??= LogHelpers.JoinLast(logs, LogSource.Stdout, 50);
        readiness.Stderr ??= LogHelpers.JoinLast(logs, LogSource.Stderr, 50);
        return readiness;
    }

    private Exception Translate(Exception exception, CancellationTokenSource startupTimeout, CancellationToken userToken)
    {
        if (exception is OperationCanceledException)
        {
            if (userToken.IsCancellationRequested)
            {
                return exception;
            }

            return startupTimeout.IsCancellationRequested
                ? new WslTimeoutException(
                    $"Container '{Name}' did not complete startup within {_configuration.StartupTimeout.TotalSeconds:0.###}s.",
                    exception)
                : exception;
        }

        return exception is WslcException
            ? exception
            : new WslProvisioningException($"Failed to start container '{Name}': {exception.Message}", exception);
    }

    private void OnSessionTerminated(SessionTerminationReason reason) =>
        _logs.Publish(LogLine.Diagnostic($"session terminated: {reason}"));

    private void UpdateState(string state)
    {
        if (_metadata is null)
        {
            return;
        }

        _metadata = _metadata with { State = state };
        try
        {
            _store.WriteMetadata(_metadata);
        }
        catch
        {
        }
    }

    private static void RequireContainerPath(string path, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Container path must not be empty and must be an absolute Linux path (e.g. /tmp/file).", parameterName);
        }

        if (!path.StartsWith('/'))
        {
            throw new ArgumentException($"Container path '{path}' must be an absolute Linux path starting with '/'.", parameterName);
        }
    }

    private static void ValidateExecOptions(ExecOptions? options)
    {
        if (options is null)
        {
            return;
        }

        if (options.Timeout is { } timeout && timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(options), timeout, "Exec timeout must be positive.");
        }

        if (options.Environment is not null)
        {
            foreach (var pair in options.Environment)
            {
                ValidateEnvironmentName(pair.Key);
                if (pair.Value is null)
                {
                    throw new ArgumentException($"Environment variable '{pair.Key}' has null value.", nameof(options));
                }
            }
        }

        if (options.WorkingDirectory is { } workingDirectory && string.IsNullOrWhiteSpace(workingDirectory))
        {
            throw new ArgumentException("Working directory must not be empty when set.", nameof(options));
        }
    }

    private static void ValidateEnvironmentName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ArgumentException("Environment variable name must not be empty.", nameof(name));
        }

        if (!char.IsLetter(name[0]) && name[0] != '_')
        {
            throw new ArgumentException($"Environment variable name '{name}' must start with a letter or underscore.", nameof(name));
        }

        foreach (var character in name)
        {
            if (!char.IsLetterOrDigit(character) && character != '_')
            {
                throw new ArgumentException($"Environment variable name '{name}' contains invalid character '{character}'.", nameof(name));
            }
        }
    }

    private static bool IsBenignRuntimeError(Exception exception) =>
        exception.HResult is (int)Error.ContainerNotRunning
            or (int)Error.ContainerNotFound
            or (int)Error.ContainerDeleted
            or (int)Error.SessionReserved;
}
