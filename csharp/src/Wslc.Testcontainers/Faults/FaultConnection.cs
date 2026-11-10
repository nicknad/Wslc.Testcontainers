using System.Net.Sockets;

namespace Wslc.Testcontainers.Faults;

/// <summary>
/// One relayed client/target socket pair. Owns both directional pumps plus the per-direction
/// stall gates and half-close state; the shim only routes fault calls here.
/// </summary>
internal sealed class FaultConnection : IDisposable
{
    private readonly FaultShim _owner;
    private readonly TcpClient _client;
    private readonly TcpClient _target;
    private readonly AsyncGate _clientToTargetGate = new();
    private readonly AsyncGate _targetToClientGate = new();
    private readonly CancellationTokenSource _lifetime = new();
    private readonly object _gate = new();

    // A half-close fault marks its direction so the pump drops the next chunk instead of writing
    // after Shutdown(Send). volatile because the pump reads the flag without taking the lock.
    private volatile bool _clientToTargetStopped;
    private volatile bool _targetToClientStopped;
    private bool _closed;
    private Task _completion = Task.CompletedTask;

    public FaultConnection(FaultShim owner, TcpClient client, TcpClient target)
    {
        _owner = owner;
        _client = client;
        _target = target;
    }

    /// <summary>Gets a task that completes when both pumps have stopped and sockets are closed.</summary>
    public Task Completion => _completion;

    /// <summary>Starts pumping; must be called exactly once before fault calls can target it.</summary>
    public void Start() => _completion = RunAsync();

    /// <summary>Applies one fault; returns <c>false</c> when the connection already ended.</summary>
    public bool Apply(FaultMode mode, FaultDirection direction) => mode switch
    {
        FaultMode.Reset => Reset(),
        FaultMode.Close => HalfClose(direction),
        FaultMode.Stall => Stall(direction),
        _ => false,
    };

    /// <summary>Resumes stalled directions; safe on connections that were never stalled.</summary>
    public void Resume()
    {
        _clientToTargetGate.Open();
        _targetToClientGate.Open();
    }

    /// <summary>Releases both sockets; equivalent to <see cref="Abort"/> and safe to call repeatedly.</summary>
    public void Dispose() => Abort();

    /// <summary>Best-effort teardown used by dispose: closes both ends without throwing.</summary>
    public void Abort()
    {
        lock (_gate)
        {
            if (_closed)
            {
                return;
            }

            _closed = true;
            CloseSockets();
        }

        _owner.Unregister(this);
        CancelLifetime();
    }

    private bool Reset()
    {
        // Linger and close must be applied while the connection lock is held: a pump that finishes
        // concurrently would otherwise take the lock, dispose the sockets gracefully, and turn the
        // expected RST into a FIN.
        lock (_gate)
        {
            if (_closed)
            {
                return false;
            }

            _closed = true;

            // Linger-to-zero turns Close into an abortive close, so the peer observes RST instead
            // of the FIN a graceful close would send. Both legs are aborted for a clean drop.
            AbortWithReset(_client);
            AbortWithReset(_target);
        }

        _owner.Unregister(this);
        CancelLifetime();
        return true;
    }

    private bool HalfClose(FaultDirection direction)
    {
        lock (_gate)
        {
            if (_closed)
            {
                return false;
            }
        }

        if (direction is FaultDirection.ClientToBroker or FaultDirection.Both)
        {
            _clientToTargetStopped = true;
            TryShutdownSend(_target.Client);
        }

        if (direction is FaultDirection.BrokerToClient or FaultDirection.Both)
        {
            _targetToClientStopped = true;
            TryShutdownSend(_client.Client);
        }

        return true;
    }

    private bool Stall(FaultDirection direction)
    {
        lock (_gate)
        {
            if (_closed)
            {
                return false;
            }
        }

        if (direction is FaultDirection.ClientToBroker or FaultDirection.Both)
        {
            _clientToTargetGate.Close();
        }

        if (direction is FaultDirection.BrokerToClient or FaultDirection.Both)
        {
            _targetToClientGate.Close();
        }

        return true;
    }

    private async Task RunAsync()
    {
        try
        {
            await Task.WhenAll(
                PumpAsync(_client, _target, _clientToTargetGate, clientToBroker: true),
                PumpAsync(_target, _client, _targetToClientGate, clientToBroker: false)).ConfigureAwait(false);
        }
        catch
        {
            // Both pumps absorb their own failures; this only guards stream construction.
        }
        finally
        {
            // Close under the lock so pump cleanup is serialized with Reset's linger-to-zero
            // decision: if reset already won, it sent the RST and this close is a no-op.
            lock (_gate)
            {
                _closed = true;
                CloseSockets();
            }

            _owner.Unregister(this);
            try
            {
                _lifetime.Dispose();
            }
            catch
            {
                // Best-effort: a fault may race teardown and already have released the source.
            }
        }
    }

    private async Task PumpAsync(TcpClient sourceClient, TcpClient destinationClient, AsyncGate gate, bool clientToBroker)
    {
        var buffer = new byte[FaultShim.BufferSize];
        try
        {
            var source = sourceClient.GetStream();
            var destination = destinationClient.GetStream();
            var destinationSocket = destinationClient.Client;
            var token = _lifetime.Token;

            // Per ADR bounded loops this chunk cap bounds the relay: 1 GiB / 16 KiB + 1. A peer
            // that exceeds it is treated as broken and the connection is torn down.
            for (var chunk = 0; chunk < FaultShim.MaxChunks; chunk++)
            {
                // Stall gate before the read: a stalled direction stops pulling bytes so the
                // sender's own socket buffer provides backpressure.
                await gate.WaitAsync().WaitAsync(token).ConfigureAwait(false);
                var read = await source.ReadAsync(buffer.AsMemory(), token).ConfigureAwait(false);
                if (read == 0)
                {
                    // Propagate the peer's FIN so the other end sees a half-close at the right time.
                    TryShutdownSend(destinationSocket);
                    return;
                }

                if (IsStopped(clientToBroker))
                {
                    // The half-close fault already sent FIN this way; discard the late chunk
                    // instead of writing to a socket whose send side is shut down.
                    return;
                }

                // Stall gate after the read: the chunk that raced the stall is held, not forwarded,
                // and Clear resumes it without corrupting the byte stream.
                await gate.WaitAsync().WaitAsync(token).ConfigureAwait(false);
                var delay = _owner.GetLatency(clientToBroker);
                if (delay > TimeSpan.Zero)
                {
                    await Task.Delay(delay, token).ConfigureAwait(false);
                }

                await destination.WriteAsync(buffer.AsMemory(0, read), token).ConfigureAwait(false);
            }

            CancelLifetime();
        }
        catch (Exception) when (_lifetime.IsCancellationRequested)
        {
            // Reset, abort, or dispose: both ends are going away, so ending quietly is expected.
        }
        catch
        {
            // Unexpected socket failure on one direction: abort the other direction too.
            CancelLifetime();
        }
    }

    private bool IsStopped(bool clientToBroker) =>
        clientToBroker ? _clientToTargetStopped : _targetToClientStopped;

    private static void TryShutdownSend(Socket socket)
    {
        try
        {
            socket.Shutdown(SocketShutdown.Send);
        }
        catch
        {
            // Best-effort: the peer may already have closed or reset the connection.
        }
    }

    private static void AbortWithReset(TcpClient client)
    {
        try
        {
            client.Client.LingerState = new LingerOption(enable: true, seconds: 0);
        }
        catch
        {
            // Best-effort: a socket that is already closed cannot be configured.
        }

        try
        {
            client.Client.Close();
        }
        catch
        {
            // Best-effort teardown per ADR-0004.
        }
    }

    private void CloseSockets()
    {
        try
        {
            _client.Dispose();
        }
        catch
        {
            // Best-effort teardown per ADR-0004.
        }

        try
        {
            _target.Dispose();
        }
        catch
        {
            // Best-effort teardown per ADR-0004.
        }
    }

    private void CancelLifetime()
    {
        try
        {
            _lifetime.Cancel();
        }
        catch
        {
            // Best-effort: cancellation callbacks may already have completed.
        }
    }

    /// <summary>
    /// Manual-reset async gate: open by default, closed while stalled. Waiters block on the task
    /// of the current signal, so opening wakes every pump and closing parks new waits.
    /// </summary>
    private sealed class AsyncGate
    {
        private readonly object _sync = new();
        private TaskCompletionSource _signal = CreateOpenSignal();

        public Task WaitAsync()
        {
            lock (_sync)
            {
                return _signal.Task;
            }
        }

        public void Close()
        {
            lock (_sync)
            {
                if (_signal.Task.IsCompleted)
                {
                    _signal = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                }
            }
        }

        public void Open()
        {
            lock (_sync)
            {
                _ = _signal.TrySetResult();
            }
        }

        private static TaskCompletionSource CreateOpenSignal()
        {
            var signal = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            signal.SetResult();
            return signal;
        }
    }
}
