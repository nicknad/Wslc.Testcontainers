using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Faults;
using Xunit;

namespace Wslc.Testcontainers.Tests.Faults;

/// <summary>
/// Drives <see cref="FaultShim"/> through its public API against a plain loopback echo server.
/// No container runtime or WSL is involved, so socket semantics are tested cheaply.
/// </summary>
public sealed class FaultShimTests
{
    private static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(5);

    [Fact]
    public async Task Attach_binds_loopback_ephemeral_port_and_forwards_bytes()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        Assert.Equal(AddressFamily.InterNetwork, shim.Endpoint.AddressFamily);
        Assert.Equal(IPAddress.Loopback, shim.Endpoint.Address);
        Assert.True(shim.Endpoint.Port > 0);
        Assert.NotEqual(echo.Port, shim.Endpoint.Port);
        Assert.Equal(0, shim.LiveConnections);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        Assert.Equal("ping", await RoundTripAsync(stream, "ping"));

        await WaitUntilAsync(() => shim.LiveConnections == 1, "one live relayed connection");
    }

    [Fact]
    public async Task Inject_reset_makes_the_client_observe_a_reset()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        await RoundTripAsync(stream, "warm");
        await WaitUntilAsync(() => shim.LiveConnections == 1, "one live relayed connection");

        Assert.Equal(1, shim.Inject(FaultMode.Reset));
        await ExpectResetAsync(stream);
        await WaitUntilAsync(() => shim.LiveConnections == 0, "reset connection to leave the relay");
    }

    [Fact]
    public async Task Inject_close_send_shuts_client_to_broker_while_reverse_direction_flows()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        await RoundTripAsync(stream, "open");

        Assert.Equal(1, shim.Inject(FaultMode.Close, FaultDirection.ClientToBroker));

        // The echo server sees EOF on its read side and answers with the marker, which proves the
        // broker-to-client direction is still open after the half-close.
        var payload = await ReadExactlyAsync(stream, 3, DefaultTimeout);
        Assert.Equal("bye", Encoding.ASCII.GetString(payload));
    }

    [Fact]
    public async Task Inject_close_on_broker_to_client_keeps_client_to_broker_flowing()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        await RoundTripAsync(stream, "open");

        Assert.Equal(1, shim.Inject(FaultMode.Close, FaultDirection.BrokerToClient));

        var cancellationToken = TestContext.Current.CancellationToken;

        // The client observes a clean FIN (zero bytes), not a reset.
        var buffer = new byte[1];
        var read = await stream.ReadAsync(buffer, cancellationToken).AsTask().WaitAsync(DefaultTimeout, cancellationToken);
        Assert.Equal(0, read);

        // The client can still send, and the echo server still receives those bytes.
        await stream.WriteAsync("after"u8.ToArray(), cancellationToken).AsTask().WaitAsync(DefaultTimeout, cancellationToken);
        await WaitUntilAsync(
            () => echo.Received.Contains("after", StringComparison.Ordinal),
            "the target to receive bytes after the broker-to-client half-close");
    }

    [Fact]
    public async Task Inject_stall_holds_bytes_until_clear_resumes_the_stream()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        await RoundTripAsync(stream, "warm");

        Assert.Equal(1, shim.Inject(FaultMode.Stall, FaultDirection.BrokerToClient));

        var cancellationToken = TestContext.Current.CancellationToken;

        // The echo server answers "held" immediately, but the relay must hold the chunk while the
        // direction is stalled; nothing may arrive at the client in the meantime.
        await stream.WriteAsync("held"u8.ToArray(), cancellationToken).AsTask().WaitAsync(DefaultTimeout, cancellationToken);
        var buffer = new byte[4];
        var readTask = stream.ReadAsync(buffer.AsMemory(0, 4), cancellationToken).AsTask();
        var winner = await Task.WhenAny(readTask, Task.Delay(TimeSpan.FromMilliseconds(300), cancellationToken));
        Assert.NotSame(readTask, winner);

        shim.Clear();

        var read = await readTask.WaitAsync(DefaultTimeout, cancellationToken);
        Assert.Equal(4, read);
        Assert.Equal("held", Encoding.ASCII.GetString(buffer, 0, read));
    }

    [Fact]
    public async Task Latency_delays_broker_to_client_chunks()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();
        await RoundTripAsync(stream, "warm");

        shim.Latency(TimeSpan.FromMilliseconds(200), FaultDirection.BrokerToClient);

        var stopwatch = Stopwatch.StartNew();
        var payload = await RoundTripAsync(stream, "delay");
        stopwatch.Stop();

        Assert.Equal("delay", payload);
        Assert.True(
            stopwatch.Elapsed >= TimeSpan.FromMilliseconds(150),
            $"A 200ms broker-to-client delay must be observable; the round trip took {stopwatch.Elapsed}.");
    }

    [Fact]
    public async Task Arm_faults_exactly_the_next_connections_then_stops()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        shim.Arm(2, FaultMode.Reset);

        for (var index = 0; index < 2; index++)
        {
            using var resetClient = await ConnectAsync(shim);
            await ExpectResetAsync(resetClient.GetStream());
        }

        using var working = await ConnectAsync(shim);
        Assert.Equal("ok", await RoundTripAsync(working.GetStream(), "ok"));

        // The arm is spent; a fourth connection must not be faulted.
        using var later = await ConnectAsync(shim);
        Assert.Equal("ok", await RoundTripAsync(later.GetStream(), "ok"));
    }

    [Fact]
    public async Task Inject_returns_the_number_of_live_connections_and_zero_when_none()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        Assert.Equal(0, shim.Inject(FaultMode.Stall));

        var first = await ConnectAsync(shim);
        var second = await ConnectAsync(shim);
        try
        {
            await RoundTripAsync(first.GetStream(), "one");
            await RoundTripAsync(second.GetStream(), "two");
            await WaitUntilAsync(() => shim.LiveConnections == 2, "two live relayed connections");

            Assert.Equal(2, shim.Inject(FaultMode.Stall));
            shim.Clear();
            Assert.Equal(2, shim.Inject(FaultMode.Stall));
            shim.Clear();
        }
        finally
        {
            first.Dispose();
            second.Dispose();
        }

        await WaitUntilAsync(() => shim.LiveConnections == 0, "relayed connections to close");
        Assert.Equal(0, shim.Inject(FaultMode.Stall));
    }

    [Fact]
    public async Task Clear_is_idempotent_and_drops_latency_and_armed_faults()
    {
        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        shim.Clear();
        shim.Clear();

        shim.Arm(1, FaultMode.Reset);
        shim.Latency(TimeSpan.FromSeconds(5), FaultDirection.BrokerToClient);
        shim.Clear();

        using var client = await ConnectAsync(shim);
        var stream = client.GetStream();

        // A fresh connection must neither be reset (the arm is gone) nor take five seconds
        // (the latency is gone). The bounded read timeout fails fast if either survives.
        Assert.Equal("ok", await RoundTripAsync(stream, "ok", TimeSpan.FromSeconds(2)));
    }

    [Fact]
    public async Task Dispose_is_idempotent_and_use_after_dispose_throws()
    {
        var echo = new EchoServer();
        var shim = FaultShim.Attach(echo.Endpoint);
        try
        {
            using var client = await ConnectAsync(shim);
            await RoundTripAsync(client.GetStream(), "warm");
            _ = shim.Inject(FaultMode.Stall);

            await shim.DisposeAsync();
            await shim.DisposeAsync();

            Assert.Throws<ObjectDisposedException>(() => shim.Inject(FaultMode.Reset));
            Assert.Throws<ObjectDisposedException>(() => shim.Arm(1, FaultMode.Reset));
            Assert.Throws<ObjectDisposedException>(() => shim.Latency(TimeSpan.FromMilliseconds(1)));
            Assert.Throws<ObjectDisposedException>(shim.Clear);
            Assert.Throws<ObjectDisposedException>(() => _ = shim.LiveConnections);
            Assert.Throws<ObjectDisposedException>(() => _ = shim.Endpoint);
        }
        finally
        {
            await echo.DisposeAsync();
        }
    }

    [Fact]
    public async Task Invalid_arguments_are_rejected()
    {
        Assert.Throws<ArgumentNullException>(() => FaultShim.Attach(null!));

        await using var echo = new EchoServer();
        await using var shim = FaultShim.Attach(echo.Endpoint);

        Assert.Throws<ArgumentOutOfRangeException>(() => shim.Arm(-1, FaultMode.Reset));
        Assert.Throws<ArgumentOutOfRangeException>(() => shim.Latency(TimeSpan.FromMilliseconds(-1)));
        Assert.Throws<ArgumentOutOfRangeException>(() => shim.Inject((FaultMode)99));
        Assert.Throws<ArgumentOutOfRangeException>(() => shim.Inject(FaultMode.Reset, (FaultDirection)99));
        Assert.Throws<ArgumentOutOfRangeException>(() => shim.Arm(1, (FaultMode)99));
    }

    private static async Task<TcpClient> ConnectAsync(FaultShim shim)
    {
        var client = new TcpClient();
        await client.ConnectAsync(shim.Endpoint).WaitAsync(DefaultTimeout);
        client.NoDelay = true;
        return client;
    }

    private static async Task<string> RoundTripAsync(NetworkStream stream, string text, TimeSpan? timeout = null)
    {
        var payload = Encoding.ASCII.GetBytes(text);
        await stream.WriteAsync(payload).AsTask().WaitAsync(timeout ?? DefaultTimeout);
        var echoed = await ReadExactlyAsync(stream, payload.Length, timeout ?? DefaultTimeout);
        return Encoding.ASCII.GetString(echoed);
    }

    private static async Task<byte[]> ReadExactlyAsync(NetworkStream stream, int count, TimeSpan timeout)
    {
        var buffer = new byte[count];
        var read = 0;

        // The bound is the byte count: each successful read consumes at least one byte.
        for (var remaining = count; remaining > 0;)
        {
            var chunk = await stream.ReadAsync(buffer.AsMemory(read, remaining)).AsTask().WaitAsync(timeout);
            if (chunk == 0)
            {
                Assert.Fail($"Connection closed after {read} of {count} expected bytes.");
            }

            read += chunk;
            remaining -= chunk;
        }

        return buffer;
    }

    private static async Task ExpectResetAsync(NetworkStream stream)
    {
        var buffer = new byte[16];

        // A reset is observable either directly on the next read. A FIN (zero bytes) is a failed
        // expectation because the relay must not fall back to a graceful close.
        for (var attempt = 0; attempt < 4; attempt++)
        {
            try
            {
                var read = await stream.ReadAsync(buffer).AsTask().WaitAsync(DefaultTimeout);
                if (read == 0)
                {
                    Assert.Fail("Expected a TCP reset but the peer closed gracefully (FIN).");
                }
            }
            catch (Exception exception) when (IsConnectionReset(exception))
            {
                return;
            }
        }

        Assert.Fail("Expected a TCP reset from the relay.");
    }

    private static bool IsConnectionReset(Exception exception) =>
        exception switch
        {
            SocketException socket => socket.SocketErrorCode == SocketError.ConnectionReset,
            IOException io => io.InnerException is not null && IsConnectionReset(io.InnerException),
            _ => false,
        };

    private static async Task WaitUntilAsync(Func<bool> condition, string description)
    {
        // 500 attempts at 10ms bound the wait at ~5s without an unbounded polling loop.
        for (var attempt = 0; attempt < 500; attempt++)
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(10);
        }

        Assert.Fail($"Timed out waiting for {description}.");
    }

    /// <summary>Loopback echo server: echoes bytes, records them, and answers EOF with "bye".</summary>
    private sealed class EchoServer : IAsyncDisposable
    {
        private const int MaxAcceptedConnections = 32;
        private const int MaxEchoChunks = 1024;
        private const string ByeMarker = "bye";

        private readonly TcpListener _listener = new(IPAddress.Loopback, 0);
        private readonly CancellationTokenSource _lifetime = new();
        private readonly List<TcpClient> _clients = [];
        private readonly List<Task> _handlers = [];
        private readonly StringBuilder _received = new();
        private readonly object _gate = new();
        private readonly Task _acceptLoop;

        public EchoServer()
        {
            _listener.Start();
            Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
            _acceptLoop = AcceptLoopAsync();
        }

        public int Port { get; }

        public IPEndPoint Endpoint => new(IPAddress.Loopback, Port);

        public string Received
        {
            get
            {
                lock (_gate)
                {
                    return _received.ToString();
                }
            }
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                await _lifetime.CancelAsync();
            }
            catch
            {
                // Best-effort test cleanup.
            }

            try
            {
                _listener.Stop();
            }
            catch
            {
                // Best-effort test cleanup.
            }

            Task[] handlers;
            lock (_gate)
            {
                foreach (var client in _clients)
                {
                    try
                    {
                        client.Dispose();
                    }
                    catch
                    {
                        // Best-effort test cleanup.
                    }
                }

                handlers = [.. _handlers];
            }

            try
            {
                await Task.WhenAll(handlers).WaitAsync(DefaultTimeout);
            }
            catch
            {
                // Best-effort test cleanup.
            }

            try
            {
                await _acceptLoop.WaitAsync(DefaultTimeout);
            }
            catch
            {
                // Best-effort test cleanup.
            }

            _lifetime.Dispose();
        }

        private async Task AcceptLoopAsync()
        {
            for (var accepted = 0; accepted < MaxAcceptedConnections && !_lifetime.IsCancellationRequested; accepted++)
            {
                TcpClient client;
                try
                {
                    client = await _listener.AcceptTcpClientAsync(_lifetime.Token);
                }
                catch
                {
                    return;
                }

                client.NoDelay = true;
                lock (_gate)
                {
                    _clients.Add(client);
                    _handlers.Add(HandleAsync(client));
                }
            }
        }

        private async Task HandleAsync(TcpClient client)
        {
            try
            {
                var stream = client.GetStream();
                var buffer = new byte[4096];
                for (var chunk = 0; chunk < MaxEchoChunks; chunk++)
                {
                    var read = await stream.ReadAsync(buffer, _lifetime.Token);
                    if (read == 0)
                    {
                        break;
                    }

                    lock (_gate)
                    {
                        _ = _received.Append(Encoding.ASCII.GetString(buffer, 0, read));
                    }

                    await stream.WriteAsync(buffer.AsMemory(0, read), _lifetime.Token);
                }

                // Half-close tests rely on this marker proving the reverse direction still flows.
                await stream.WriteAsync(Encoding.ASCII.GetBytes(ByeMarker), _lifetime.Token);
            }
            catch
            {
                // Reset, cancellation, or disposal ends the handler quietly.
            }
            finally
            {
                client.Dispose();
            }
        }
    }
}
