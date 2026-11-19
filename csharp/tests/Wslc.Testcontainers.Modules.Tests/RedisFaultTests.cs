using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Faults;
using Wslc.Testcontainers.Modules.Redis;
using Wslc.Testcontainers.Testing;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

/// <summary>
/// Composes <see cref="FaultShim"/> with the Redis preset: raw RESP commands from a hand-rolled
/// TCP client that connects through the relay to a live server, so reset, stall and latency are
/// proven against real Redis behavior rather than an echo server. The server's direct endpoint
/// stays usable throughout, which keeps the readiness and triage path honest.
/// </summary>
public sealed class RedisFaultTests
{
    // Every socket step must fail in seconds so a wedged relay or server surfaces quickly; the
    // integration fact itself owns the multi-minute container-start budget.
    private static readonly TimeSpan StepTimeout = TimeSpan.FromSeconds(5);

    // A stalled direction must hold a reply for longer than any healthy round trip could take.
    private const int StallObservationMilliseconds = 500;

    // A reply line longer than this means the protocol reader is misframing, not a big reply.
    private const int MaxReplyLineBytes = 512;

    [IntegrationFact]
    public async Task Set_and_get_round_trip_through_the_relay()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var redis = new RedisBuilder().Build();
        await redis.StartAsync(cancellationToken);

        await using var shim = FaultShim.Attach(redis.GetConnectEndpoint(RedisContainer.DefaultPort));

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        var set = await SendCommandAsync(stream, ["SET", "wslc:faults:key", "relay"], cancellationToken);
        Assert.Equal("OK", Encoding.ASCII.GetString(set));

        var get = await SendCommandAsync(stream, ["GET", "wslc:faults:key"], cancellationToken);
        Assert.Equal("relay", Encoding.ASCII.GetString(get));
    }

    [IntegrationFact]
    public async Task Inject_reset_returns_one_and_the_client_socket_observes_the_reset()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var redis = new RedisBuilder().Build();
        await redis.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(redis.GetConnectEndpoint(RedisContainer.DefaultPort));

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();
        await SendCommandAsync(stream, ["PING"], cancellationToken);
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        Assert.Equal(1, shim.Inject(FaultMode.Reset));

        Assert.True(
            await ObservesResetAsync(stream, cancellationToken),
            "Expected a TCP reset from the relay but the peer closed gracefully (FIN).");
    }

    [IntegrationFact]
    public async Task Armed_stall_silences_the_next_connection_until_clear_completes_the_request()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var redis = new RedisBuilder().Build();
        await redis.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(redis.GetConnectEndpoint(RedisContainer.DefaultPort));

        shim.Arm(1, FaultMode.Stall);

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        await stream
            .WriteAsync(Command(["SET", "wslc:faults:stalled", "held"]), cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);

        // The armed stall parks both directions: no reply may arrive within the window.
        var pending = ReadReplyAsync(stream, cancellationToken);
        var winner = await Task.WhenAny(pending, Task.Delay(StallObservationMilliseconds, cancellationToken));
        Assert.NotSame(pending, winner);

        shim.Clear();

        var reply = await pending.WaitAsync(StepTimeout, cancellationToken);
        Assert.Equal("OK", Encoding.ASCII.GetString(reply));
    }

    [IntegrationFact]
    public async Task Latency_on_target_to_client_delays_the_reply_without_breaking_the_session()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var redis = new RedisBuilder().Build();
        await redis.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(redis.GetConnectEndpoint(RedisContainer.DefaultPort));

        shim.Latency(TimeSpan.FromMilliseconds(400), FaultDirection.TargetToClient);

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();

        var stopwatch = Stopwatch.StartNew();
        var reply = await SendCommandAsync(stream, ["PING"], cancellationToken);
        stopwatch.Stop();

        Assert.Equal("PONG", Encoding.ASCII.GetString(reply));
        Assert.True(
            stopwatch.Elapsed >= TimeSpan.FromMilliseconds(300),
            $"A 400ms target-to-client delay must be observable on the reply; it took {stopwatch.Elapsed}.");

        // The delayed session still works: GET of a missing key answers a nil bulk string.
        var get = await SendCommandAsync(stream, ["GET", "wslc:faults:latency"], cancellationToken);
        Assert.Empty(get);
    }

    private static async Task<TcpClient> ConnectTcpAsync(IPEndPoint endpoint, CancellationToken cancellationToken)
    {
        var client = new TcpClient();
        try
        {
            await client
                .ConnectAsync(endpoint.Address, endpoint.Port, cancellationToken)
                .AsTask()
                .WaitAsync(StepTimeout, cancellationToken);
            client.NoDelay = true;
            return client;
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    private static async Task<byte[]> SendCommandAsync(
        NetworkStream stream,
        string[] parts,
        CancellationToken cancellationToken)
    {
        await stream
            .WriteAsync(Command(parts), cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);
        return await ReadReplyAsync(stream, cancellationToken);
    }

    private static byte[] Command(string[] parts)
    {
        // RESP request framing: an array header, then one bulk string per argument.
        var builder = new StringBuilder();
        builder.Append('*').Append(parts.Length).Append("\r\n");
        foreach (var part in parts)
        {
            var bytes = Encoding.UTF8.GetBytes(part);
            builder.Append('$').Append(bytes.Length).Append("\r\n").Append(part).Append("\r\n");
        }

        return Encoding.UTF8.GetBytes(builder.ToString());
    }

    private static async Task<byte[]> ReadReplyAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        var type = new byte[1];
        await stream
            .ReadExactlyAsync(type, cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);
        var header = await ReadLineAsync(stream, cancellationToken);

        return (char)type[0] switch
        {
            // Simple string (e.g. +OK, +PONG) or integer.
            '+' or ':' => Encoding.ASCII.GetBytes(header),
            '$' => await ReadBulkAsync(stream, header, cancellationToken),
            _ => throw new IOException($"Unexpected RESP reply type '{(char)type[0]}' with header '{header}'."),
        };
    }

    private static async Task<byte[]> ReadBulkAsync(
        NetworkStream stream,
        string header,
        CancellationToken cancellationToken)
    {
        var length = int.Parse(header, CultureInfo.InvariantCulture);
        if (length < 0)
        {
            // A nil bulk string (GET of a missing key).
            return Array.Empty<byte>();
        }

        // Payload plus the trailing CRLF, which is dropped from the returned bytes.
        var payload = new byte[length + 2];
        await stream
            .ReadExactlyAsync(payload, cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);
        return payload[..^2];
    }

    private static async Task<string> ReadLineAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        // Byte-at-a-time is protocol-safe and cheap for the short replies these tests read.
        var line = new List<byte>(64);
        for (var read = 0; read < MaxReplyLineBytes; read++)
        {
            var buffer = new byte[1];
            if (await stream
                .ReadAsync(buffer, cancellationToken)
                .AsTask()
                .WaitAsync(StepTimeout, cancellationToken) == 0)
            {
                throw new IOException("The connection closed before a complete reply line arrived.");
            }

            if (buffer[0] == (byte)'\n')
            {
                if (line.Count > 0 && line[^1] == (byte)'\r')
                {
                    line.RemoveAt(line.Count - 1);
                }

                return Encoding.ASCII.GetString([.. line]);
            }

            line.Add(buffer[0]);
        }

        throw new IOException($"A reply line exceeded {MaxReplyLineBytes} bytes.");
    }

    private static async Task<bool> ObservesResetAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        var buffer = new byte[64];

        // A reset is observable on the next read. A FIN (zero bytes) or a timeout is a failed
        // expectation because the relay must not fall back to a graceful close.
        for (var attempt = 0; attempt < 4; attempt++)
        {
            try
            {
                var read = await stream
                    .ReadAsync(buffer, cancellationToken)
                    .AsTask()
                    .WaitAsync(StepTimeout, cancellationToken);
                if (read == 0)
                {
                    return false;
                }
            }
            catch (Exception exception) when (IsConnectionReset(exception))
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsConnectionReset(Exception exception) =>
        exception switch
        {
            SocketException socket => socket.SocketErrorCode == SocketError.ConnectionReset,
            IOException io => io.InnerException is not null && IsConnectionReset(io.InnerException),
            _ => false,
        };

    private static async Task WaitUntilAsync(Func<bool> condition, CancellationToken cancellationToken)
    {
        // 500 attempts at 10ms bound the wait at ~5s without an unbounded polling loop.
        for (var attempt = 0; attempt < 500; attempt++)
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(10, cancellationToken);
        }

        Assert.Fail("Timed out waiting for the relayed connection to register.");
    }
}
