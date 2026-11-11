using System.Net;
using System.Net.Sockets;

namespace Wslc.Testcontainers.Faults;

/// <summary>
/// In-process TCP relay that listens on Windows loopback, forwards every accepted connection to
/// a target endpoint, and injects honest transport faults (reset, half-close, stall, latency) on
/// demand without any helper inside the container.
/// </summary>
/// <remarks>
/// <para>
/// The relay is protocol-blind and opt-in: it never inspects or fabricates broker protocol
/// messages, and the container endpoint remains usable directly. One relay connection maps 1:1 to
/// one target connection, so a broker observes one session per client session.
/// </para>
/// <para>
/// Attach after the container is started. The shim binds an ephemeral IPv4 loopback port, so it
/// never pins a fixed port and parallel test runs cannot collide. Dispose the shim before the
/// container; after a stop/start cycle the mapped host port may change, so attach a new shim.
/// </para>
/// <para>
/// Bounds: at most 255 concurrently relayed connections, a 16 KiB per-direction buffer, a cap on
/// bytes relayed per connection, and a fixed lifetime cap on accepted connections. A client that
/// exceeds a cap is disconnected instead of consuming unbounded memory or threads.
/// </para>
/// <para>
/// Fault state is process-local to this shim and safe to call from multiple threads. Faults apply
/// to live connections only; armed faults apply to connections accepted afterwards.
/// </para>
/// </remarks>
public sealed class FaultShim : IAsyncDisposable
{
    // 255 concurrent relayed connections bound the number of accepted sockets and pumps; a test
    // suite needs a handful, while a runaway client cannot exhaust the machine's handles.
    private const int MaxLiveConnections = 255;

    // Per ADR bounded loops we don't allow while(true): the accept loop is capped at this many
    // connections per shim lifetime (well beyond any test) and also stops on cancellation.
    private const int MaxAcceptedConnections = 100_000;

    // Per ADR bounded loops, each pump relays at most this many chunks. 1 GiB / 16 KiB + 1 bounds
    // both the bytes and the iterations so a misbehaving peer cannot pump forever.
    internal const int BufferSize = 16 * 1024;
    private const long MaxRelayedBytes = 1024L * 1024 * 1024;
    internal const int MaxChunks = (int)(MaxRelayedBytes / BufferSize) + 1;

    // Teardown waits for listener and pumps to observe the closed sockets; 5s is far more than
    // loopback needs, and the cap keeps DisposeAsync from hanging a failing test run (ADR-0004).
    private static readonly TimeSpan DisposeDrainTimeout = TimeSpan.FromSeconds(5);

    private readonly IPEndPoint _target;
    private readonly TcpListener _listener;
    private readonly IPEndPoint _endpoint;
    private readonly SemaphoreSlim _slots = new(MaxLiveConnections, MaxLiveConnections);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly object _gate = new();
    private readonly HashSet<FaultConnection> _connections = [];
    private readonly Task _acceptLoop;

    private FaultMode? _armedMode;
    private FaultDirection _armedDirection = FaultDirection.Both;
    private int _armedRemaining;
    private TimeSpan _latency = TimeSpan.Zero;
    private FaultDirection _latencyDirection = FaultDirection.Both;
    private int _disposed;

    private FaultShim(IPEndPoint target, TcpListener listener, IPEndPoint endpoint)
    {
        _target = target;
        _listener = listener;
        _endpoint = endpoint;
        _acceptLoop = AcceptLoopAsync();
    }

    /// <summary>Gets the loopback endpoint a test client connects to instead of the target.</summary>
    /// <remarks>Returns a fresh <see cref="IPEndPoint"/>; binding address is always <c>127.0.0.1</c>.</remarks>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    public IPEndPoint Endpoint
    {
        get
        {
            ThrowIfDisposed();
            return new IPEndPoint(_endpoint.Address, _endpoint.Port);
        }
    }

    /// <summary>Gets the number of currently relayed (open) connections.</summary>
    /// <remarks>This is cheap state: it takes a short lock and performs no I/O.</remarks>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    public int LiveConnections
    {
        get
        {
            ThrowIfDisposed();
            lock (_gate)
            {
                return _connections.Count;
            }
        }
    }

    /// <summary>
    /// Binds an ephemeral IPv4 loopback port and starts forwarding accepted connections to
    /// <paramref name="target"/>.
    /// </summary>
    /// <param name="target">The loopback endpoint to dial for each accepted client connection.</param>
    /// <returns>A started shim that owns the listener and every relayed socket.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="target"/> is <c>null</c>.</exception>
    /// <exception cref="SocketException">The loopback listener could not be bound.</exception>
    public static FaultShim Attach(IPEndPoint target)
    {
        ArgumentNullException.ThrowIfNull(target);

        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            var endpoint = (IPEndPoint)listener.LocalEndpoint;
            return new FaultShim(target, listener, endpoint);
        }
        catch
        {
            listener.Stop();
            throw;
        }
    }

    /// <summary>Applies a fault immediately to every live relayed connection.</summary>
    /// <param name="mode">Fault to apply.</param>
    /// <param name="direction">Direction the fault applies to; defaults to both.</param>
    /// <returns>
    /// The number of live connections the fault was applied to. Zero means nothing was connected;
    /// it is not an error.
    /// </returns>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="mode"/> or <paramref name="direction"/> is not a defined value.
    /// </exception>
    public int Inject(FaultMode mode, FaultDirection direction = FaultDirection.Both)
    {
        ThrowIfDisposed();
        ValidateMode(mode);
        ValidateDirection(direction);

        FaultConnection[] connections;
        lock (_gate)
        {
            connections = new FaultConnection[_connections.Count];
            _connections.CopyTo(connections);
        }

        var affected = 0;
        foreach (var connection in connections)
        {
            if (connection.Apply(mode, direction))
            {
                affected++;
            }
        }

        return affected;
    }

    /// <summary>Applies a fault at accept time to the next <paramref name="connections"/> clients.</summary>
    /// <remarks>
    /// Replaces any previously armed fault. <paramref name="connections"/> of zero disarms it.
    /// The counter decrements once per accepted relayed connection and never fires again after it
    /// reaches zero, so retry/backoff storms can be made deterministic.
    /// </remarks>
    /// <param name="connections">Number of accepted connections to fault; must be non-negative.</param>
    /// <param name="mode">Fault to apply at accept time.</param>
    /// <param name="direction">Direction the fault applies to; defaults to both.</param>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="connections"/> is negative, or <paramref name="mode"/>/<paramref name="direction"/>
    /// is not a defined value.
    /// </exception>
    public void Arm(int connections, FaultMode mode, FaultDirection direction = FaultDirection.Both)
    {
        ThrowIfDisposed();
        ArgumentOutOfRangeException.ThrowIfNegative(connections);
        ValidateMode(mode);
        ValidateDirection(direction);

        lock (_gate)
        {
            if (connections == 0)
            {
                _armedMode = null;
                _armedRemaining = 0;
            }
            else
            {
                _armedMode = mode;
                _armedDirection = direction;
                _armedRemaining = connections;
            }
        }
    }

    /// <summary>Delays every relayed chunk in the selected direction by a fixed duration.</summary>
    /// <remarks>
    /// The delay applies to chunks forwarded after the call, including chunks on live connections;
    /// bytes are delayed, never dropped or reordered. A zero delay clears the latency.
    /// </remarks>
    /// <param name="delay">Non-negative per-chunk delay.</param>
    /// <param name="direction">Direction the delay applies to; defaults to both.</param>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="delay"/> is negative, or <paramref name="direction"/> is not a defined value.
    /// </exception>
    public void Latency(TimeSpan delay, FaultDirection direction = FaultDirection.Both)
    {
        ThrowIfDisposed();
        ArgumentOutOfRangeException.ThrowIfLessThan(delay, TimeSpan.Zero);
        ValidateDirection(direction);

        lock (_gate)
        {
            _latency = delay;
            _latencyDirection = direction;
        }
    }

    /// <summary>Drops latency and armed faults, and resumes every stalled direction.</summary>
    /// <remarks>
    /// Idempotent and safe when nothing is armed or live. Connections already reset or half-closed
    /// stay that way: those TCP transitions cannot be undone.
    /// </remarks>
    /// <exception cref="ObjectDisposedException">The shim has been disposed.</exception>
    public void Clear()
    {
        ThrowIfDisposed();

        FaultConnection[] connections;
        lock (_gate)
        {
            _latency = TimeSpan.Zero;
            _latencyDirection = FaultDirection.Both;
            _armedMode = null;
            _armedRemaining = 0;
            _armedDirection = FaultDirection.Both;
            connections = new FaultConnection[_connections.Count];
            _connections.CopyTo(connections);
        }

        foreach (var connection in connections)
        {
            connection.Resume();
        }
    }

    /// <summary>
    /// Stops accepting, closes every relayed connection, and releases the listener. Idempotent,
    /// best-effort, and never throws (per ADR-0004 best-effort cleanup).
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        try
        {
            _lifetime.Cancel();
        }
        catch
        {
            // Best-effort: a cancellation callback may already have torn down the listener.
        }

        try
        {
            _listener.Stop();
        }
        catch
        {
            // Best-effort: the listener is already closed or was never started.
        }

        FaultConnection[] connections;
        lock (_gate)
        {
            connections = new FaultConnection[_connections.Count];
            _connections.CopyTo(connections);
            _connections.Clear();
            _armedMode = null;
            _armedRemaining = 0;
            _latency = TimeSpan.Zero;
        }

        var tasks = new Task[connections.Length + 1];
        tasks[0] = _acceptLoop;
        for (var index = 0; index < connections.Length; index++)
        {
            var connection = connections[index];
            try
            {
                connection.Abort();
            }
            catch
            {
                // Best-effort: a socket that is already gone needs no cleanup.
            }

            tasks[index + 1] = connection.Completion;
        }

        try
        {
            await Task.WhenAll(tasks).WaitAsync(DisposeDrainTimeout).ConfigureAwait(false);
        }
        catch
        {
            // Best-effort: abandoned pumps observe the closed sockets and end on their own.
        }

        try
        {
            _lifetime.Dispose();
        }
        catch
        {
            // Best-effort: disposal races with a fault cannot fail the caller.
        }
    }

    /// <summary>Returns the configured latency for a direction, or zero when none applies.</summary>
    internal TimeSpan GetLatency(bool clientToBroker)
    {
        lock (_gate)
        {
            var applies = _latencyDirection == FaultDirection.Both
                || _latencyDirection == (clientToBroker ? FaultDirection.ClientToBroker : FaultDirection.BrokerToClient);
            return applies ? _latency : TimeSpan.Zero;
        }
    }

    /// <summary>Registers a started connection so fault calls can reach it.</summary>
    internal bool Register(FaultConnection connection)
    {
        lock (_gate)
        {
            if (Volatile.Read(ref _disposed) != 0)
            {
                return false;
            }

            _connections.Add(connection);
            return true;
        }
    }

    /// <summary>Removes a connection that ended or was reset.</summary>
    internal void Unregister(FaultConnection connection)
    {
        lock (_gate)
        {
            _connections.Remove(connection);
        }
    }

    /// <summary>Consumes one armed fault, or returns <c>null</c> when nothing is armed.</summary>
    internal (FaultMode Mode, FaultDirection Direction)? TakeArmedFault()
    {
        lock (_gate)
        {
            if (_armedMode is not { } mode || _armedRemaining <= 0)
            {
                return null;
            }

            _armedRemaining--;
            if (_armedRemaining == 0)
            {
                _armedMode = null;
            }

            return (mode, _armedDirection);
        }
    }

    private static void ValidateMode(FaultMode mode)
    {
        if (mode is not (FaultMode.Reset or FaultMode.Close or FaultMode.Stall))
        {
            throw new ArgumentOutOfRangeException(nameof(mode), mode, "Unknown fault mode.");
        }
    }

    private static void ValidateDirection(FaultDirection direction)
    {
        if (direction is not (FaultDirection.ClientToBroker or FaultDirection.BrokerToClient or FaultDirection.Both))
        {
            throw new ArgumentOutOfRangeException(nameof(direction), direction, "Unknown fault direction.");
        }
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);

    private async Task AcceptLoopAsync()
    {
        // Per ADR bounded loops we don't allow while(true): accept at most MaxAcceptedConnections,
        // and stop immediately when dispose cancels the listener.
        for (var accepted = 0; accepted < MaxAcceptedConnections && !_lifetime.IsCancellationRequested; accepted++)
        {
            TcpClient client;
            try
            {
                client = await _listener.AcceptTcpClientAsync(_lifetime.Token).ConfigureAwait(false);
            }
            catch
            {
                // Dispose stops the listener; the accept loop ends quietly.
                return;
            }

            if (!_slots.Wait(0))
            {
                // At the concurrent cap: refuse the client instead of queuing unbounded work.
                client.Dispose();
                continue;
            }

            _ = RelayAsync(client);
        }
    }

    private async Task RelayAsync(TcpClient client)
    {
        var target = new TcpClient();
        FaultConnection? connection = null;
        try
        {
            await target.ConnectAsync(_target, _lifetime.Token).ConfigureAwait(false);
            target.NoDelay = true;
            client.NoDelay = true;

            connection = new FaultConnection(this, client, target);
            connection.Start();

            if (!Register(connection))
            {
                // Dispose won the race: never relay a connection on a disposed shim.
                connection.Abort();
                await connection.Completion.ConfigureAwait(false);
                return;
            }

            if (TakeArmedFault() is { } armed)
            {
                _ = connection.Apply(armed.Mode, armed.Direction);
            }

            await connection.Completion.ConfigureAwait(false);
        }
        catch
        {
            // Best-effort: a dial failure or dispose race closes both ends without surfacing an
            // unobserved task exception.
            connection?.Abort();
            try
            {
                client.Dispose();
            }
            catch
            {
            }

            try
            {
                target.Dispose();
            }
            catch
            {
            }
        }
        finally
        {
            _slots.Release();
        }
    }
}
