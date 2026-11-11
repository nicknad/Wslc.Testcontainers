using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Faults;
using Wslc.Testcontainers.Modules.Mosquitto;
using Wslc.Testcontainers.Testing;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

/// <summary>
/// Composes <see cref="FaultShim"/> with the Mosquitto preset: raw MQTT frames from a client that
/// connects through the relay to a live broker, so reset, stall and latency are proven against real
/// MQTT behavior (last-will, PINGRESP, CONNACK) rather than an echo server. The broker's direct
/// endpoint stays usable throughout, which keeps the readiness and triage path honest.
/// </summary>
public sealed class MosquittoFaultTests
{
    // MQTT 3.1.1 control packet type bytes. PUBLISH is the QoS 0 form, SUBSCRIBE has the
    // mandatory reserved bits 0b10 set.
    private const byte ConnectPacketType = 0x10;
    private const byte ConnackPacketType = 0x20;
    private const byte PublishPacketType = 0x30;
    private const byte SubscribePacketType = 0x82;
    private const byte SubackPacketType = 0x90;
    private const byte PingReqPacketType = 0xC0;
    private const byte PingRespPacketType = 0xD0;

    private const string SessionTopic = "wslc/faults/session";
    private const string LastWillTopic = "wslc/faults/last-will";
    private const string LastWillPayload = "wslc-client-crashed";

    // Every socket step must fail in seconds so a wedged relay or broker surfaces quickly; the
    // integration fact itself owns the multi-minute container-start budget.
    private static readonly TimeSpan StepTimeout = TimeSpan.FromSeconds(5);

    // Keepalive 1s plus a probe every 400ms keeps Mosquitto's 1.5x keepalive expiry away while the
    // broker-to-client direction is stalled. Two seconds without a PINGRESP is twice the keepalive,
    // so a real client would already have declared the connection dead.
    private const ushort ShortKeepAliveSeconds = 1;
    private const int KeepAliveProbeMilliseconds = 400;
    private const int StallObservationMilliseconds = 2000;

    [IntegrationFact]
    public async Task Connects_through_the_relay_and_completes_a_session()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);

        await using var shim = FaultShim.Attach(broker.GetConnectEndpoint(MosquittoContainer.DefaultPort));

        var connectionString = broker.GetConnectionString(shim.Endpoint);
        Assert.Equal($"mqtt://127.0.0.1:{shim.Endpoint.Port}", connectionString);

        using var client = await ConnectTcpAsync(EndpointFromConnectionString(connectionString), cancellationToken);
        var stream = client.GetStream();
        await ConnectMqttAsync(stream, "wslc-relay-session", cancellationToken);
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        await SubscribeAsync(stream, packetId: 1, SessionTopic, cancellationToken);
        await WriteAsync(stream, PublishPacket(SessionTopic, "hello through the relay"u8), cancellationToken);

        var published = await ReadPacketAsync(stream, cancellationToken);
        Assert.Equal(PublishPacketType, published.Header);
        var (topic, payload) = ParsePublish(published.Body);
        Assert.Equal(SessionTopic, topic);
        Assert.Equal("hello through the relay", payload);
    }

    [IntegrationFact]
    public async Task Inject_reset_drops_the_client_and_the_subscriber_receives_its_last_will()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);
        var directEndpoint = broker.GetConnectEndpoint(MosquittoContainer.DefaultPort);
        await using var shim = FaultShim.Attach(directEndpoint);

        using var subscriber = await ConnectTcpAsync(directEndpoint, cancellationToken);
        var subscriberStream = subscriber.GetStream();
        await ConnectMqttAsync(subscriberStream, "wslc-will-subscriber", cancellationToken);
        await SubscribeAsync(subscriberStream, packetId: 1, LastWillTopic, cancellationToken);

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var clientStream = client.GetStream();
        await ConnectMqttAsync(
            clientStream,
            "wslc-will-client",
            cancellationToken,
            keepAliveSeconds: 60,
            will: (LastWillTopic, LastWillPayload));
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        Assert.Equal(1, shim.Inject(FaultMode.Reset));

        await ExpectResetAsync(clientStream, cancellationToken);

        // The broker saw an ungraceful reset, so it publishes the will of the dropped client.
        var will = await ReadPacketAsync(subscriberStream, cancellationToken);
        Assert.Equal(PublishPacketType, will.Header);
        var (topic, payload) = ParsePublish(will.Body);
        Assert.Equal(LastWillTopic, topic);
        Assert.Equal(LastWillPayload, payload);
    }

    [IntegrationFact]
    public async Task Stall_on_broker_to_client_times_out_the_client_while_the_broker_stays_alive()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(broker.GetConnectEndpoint(MosquittoContainer.DefaultPort));

        using var stalled = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stalledStream = stalled.GetStream();
        await ConnectMqttAsync(stalledStream, "wslc-stalled-client", cancellationToken, ShortKeepAliveSeconds);
        await WaitUntilAsync(() => shim.LiveConnections == 1, cancellationToken);

        Assert.Equal(1, shim.Inject(FaultMode.Stall, FaultDirection.BrokerToClient));

        // Model a client's keepalive: keep sending PINGREQ so the broker's own 1.5x timer never
        // fires, and assert that no PINGRESP makes it back within twice the keepalive interval.
        var pendingResponse = ReadPacketAsync(stalledStream, cancellationToken);
        var observed = Stopwatch.StartNew();
        while (observed.Elapsed < TimeSpan.FromMilliseconds(StallObservationMilliseconds))
        {
            await WriteAsync(stalledStream, PingReqPacket(), cancellationToken);

            var winner = await Task.WhenAny(
                pendingResponse,
                Task.Delay(KeepAliveProbeMilliseconds, cancellationToken));
            if (ReferenceEquals(winner, pendingResponse))
            {
                var unexpected = await pendingResponse;
                Assert.Fail(
                    $"A stalled broker-to-client direction delivered MQTT packet 0x{unexpected.Header:X2}.");
            }
        }

        // The stall is directional: the socket stays open and the broker serves other clients.
        Assert.Equal(1, shim.LiveConnections);
        using var direct = await ConnectTcpAsync(
            broker.GetConnectEndpoint(MosquittoContainer.DefaultPort),
            cancellationToken);
        var directStream = direct.GetStream();
        await ConnectMqttAsync(directStream, "wslc-direct-while-stalled", cancellationToken);
        await SubscribeAsync(directStream, packetId: 1, SessionTopic, cancellationToken);

        // Clear resumes the held bytes instead of dropping them.
        shim.Clear();
        var response = await pendingResponse.WaitAsync(StepTimeout, cancellationToken);
        Assert.Equal(PingRespPacketType, response.Header);
    }

    [IntegrationFact]
    public async Task Arm_resets_the_next_two_connections_and_the_third_succeeds()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(broker.GetConnectEndpoint(MosquittoContainer.DefaultPort));

        shim.Arm(2, FaultMode.Reset);
        var endpoint = EndpointFromConnectionString(broker.GetConnectionString(shim.Endpoint));

        // The armed fault applies at accept time, so no MQTT bytes are needed to prove the socket
        // itself fails; a graceful FIN would be a contract violation.
        Assert.True(
            await ObservesResetAsync(endpoint, cancellationToken),
            "The first armed connection should have been reset at the socket level.");
        Assert.True(
            await ObservesResetAsync(endpoint, cancellationToken),
            "The second armed connection should have been reset at the socket level.");

        using var third = await ConnectTcpAsync(endpoint, cancellationToken);
        var thirdStream = third.GetStream();
        await ConnectMqttAsync(thirdStream, "wslc-arm-third", cancellationToken);
        await SubscribeAsync(thirdStream, packetId: 1, SessionTopic, cancellationToken);
    }

    [IntegrationFact]
    public async Task Latency_on_broker_to_client_delays_the_connack_without_breaking_the_session()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(broker.GetConnectEndpoint(MosquittoContainer.DefaultPort));

        shim.Latency(TimeSpan.FromMilliseconds(400), FaultDirection.BrokerToClient);

        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();

        var stopwatch = Stopwatch.StartNew();
        await ConnectMqttAsync(stream, "wslc-latency", cancellationToken, keepAliveSeconds: 60);
        stopwatch.Stop();

        Assert.True(
            stopwatch.Elapsed >= TimeSpan.FromMilliseconds(300),
            $"A 400ms broker-to-client delay must be observable on CONNACK; it took {stopwatch.Elapsed}.");

        // The delayed session still works: SUBACK arrives after the session is usable again.
        await SubscribeAsync(stream, packetId: 1, SessionTopic, cancellationToken);
    }

    [IntegrationFact]
    public async Task Clear_restores_normal_traffic_for_a_fresh_connection()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        await using var broker = new MosquittoBuilder().Build();
        await broker.StartAsync(cancellationToken);
        await using var shim = FaultShim.Attach(broker.GetConnectEndpoint(MosquittoContainer.DefaultPort));

        shim.Latency(TimeSpan.FromSeconds(10), FaultDirection.BrokerToClient);
        shim.Arm(5, FaultMode.Reset);
        shim.Clear();

        var stopwatch = Stopwatch.StartNew();
        using var client = await ConnectTcpAsync(shim.Endpoint, cancellationToken);
        var stream = client.GetStream();
        await ConnectMqttAsync(stream, "wslc-cleared", cancellationToken);
        stopwatch.Stop();

        Assert.True(
            stopwatch.Elapsed < TimeSpan.FromSeconds(2),
            $"Clear must drop latency and armed faults; the session took {stopwatch.Elapsed}.");
        await SubscribeAsync(stream, packetId: 1, SessionTopic, cancellationToken);
    }

    [IntegrationFact]
    public async Task Relay_disposes_before_the_container_and_the_direct_endpoint_keeps_working()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        var broker = new MosquittoBuilder().Build();
        FaultShim? shim = null;
        try
        {
            await broker.StartAsync(cancellationToken);
            var directEndpoint = broker.GetConnectEndpoint(MosquittoContainer.DefaultPort);
            shim = FaultShim.Attach(directEndpoint);

            using (var relayed = await ConnectTcpAsync(shim.Endpoint, cancellationToken))
            {
                var relayedStream = relayed.GetStream();
                await ConnectMqttAsync(relayedStream, "wslc-relayed", cancellationToken);
                await SubscribeAsync(relayedStream, packetId: 1, SessionTopic, cancellationToken);
            }

            // Attaching the relay must not affect the direct endpoint used by readiness/triage.
            using (var direct = await ConnectTcpAsync(directEndpoint, cancellationToken))
            {
                var directStream = direct.GetStream();
                await ConnectMqttAsync(directStream, "wslc-direct", cancellationToken);
                await SubscribeAsync(directStream, packetId: 1, SessionTopic, cancellationToken);
            }

            await shim.DisposeAsync();
            shim = null;

            // The broker is untouched by the relay's teardown, so the direct path still works.
            using (var afterRelay = await ConnectTcpAsync(directEndpoint, cancellationToken))
            {
                var afterRelayStream = afterRelay.GetStream();
                await ConnectMqttAsync(afterRelayStream, "wslc-after-relay", cancellationToken);
                await SubscribeAsync(afterRelayStream, packetId: 1, SessionTopic, cancellationToken);
            }
        }
        finally
        {
            if (shim is not null)
            {
                await shim.DisposeAsync();
            }

            await broker.DisposeAsync();
        }

        Assert.False(broker.IsStarted);
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

    private static async Task ConnectMqttAsync(
        NetworkStream stream,
        string clientId,
        CancellationToken cancellationToken,
        ushort keepAliveSeconds = 60,
        (string Topic, string Payload)? will = null)
    {
        await WriteAsync(stream, ConnectPacket(clientId, keepAliveSeconds, will), cancellationToken);

        var connack = await ReadPacketAsync(stream, cancellationToken);
        Assert.Equal(ConnackPacketType, connack.Header);
        Assert.Equal(2, connack.Body.Length);
        Assert.Equal(0, connack.Body[1]); // Session-present 0, connect return code 0 (accepted).
    }

    private static async Task<bool> ObservesResetAsync(IPEndPoint endpoint, CancellationToken cancellationToken)
    {
        TcpClient? client = null;
        try
        {
            client = await ConnectTcpAsync(endpoint, cancellationToken);
            return await ObservesResetAsync(client.GetStream(), cancellationToken);
        }
        catch (Exception exception) when (IsTransportFailure(exception))
        {
            // The reset may also arrive before the handshake finishes; that is still a socket-level
            // failure rather than a protocol rejection.
            return true;
        }
        finally
        {
            client?.Dispose();
        }
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

    private static async Task ExpectResetAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        Assert.True(
            await ObservesResetAsync(stream, cancellationToken),
            "Expected a TCP reset from the relay but the peer closed gracefully (FIN).");
    }

    private static bool IsConnectionReset(Exception exception) =>
        exception switch
        {
            SocketException socket => socket.SocketErrorCode == SocketError.ConnectionReset,
            IOException io => io.InnerException is not null && IsConnectionReset(io.InnerException),
            _ => false,
        };

    private static bool IsTransportFailure(Exception exception) =>
        exception is SocketException or IOException;

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

    private static IPEndPoint EndpointFromConnectionString(string connectionString)
    {
        var uri = new Uri(connectionString);
        return new IPEndPoint(IPAddress.Parse(uri.Host), uri.Port);
    }

    private static async Task SubscribeAsync(
        NetworkStream stream,
        ushort packetId,
        string topicFilter,
        CancellationToken cancellationToken)
    {
        await WriteAsync(stream, SubscribePacket(packetId, topicFilter, qos: 0), cancellationToken);

        var suback = await ReadPacketAsync(stream, cancellationToken);
        Assert.Equal(SubackPacketType, suback.Header);
        Assert.Equal(
            new byte[] { (byte)(packetId >> 8), (byte)packetId, 0x00 },
            suback.Body);
    }

    private static Task WriteAsync(NetworkStream stream, byte[] packet, CancellationToken cancellationToken) =>
        stream.WriteAsync(packet, cancellationToken).AsTask().WaitAsync(StepTimeout, cancellationToken);

    private static (string Topic, string Payload) ParsePublish(byte[] body)
    {
        var topicLength = (body[0] << 8) | body[1];
        var topic = Encoding.UTF8.GetString(body, 2, topicLength);
        var payload = Encoding.UTF8.GetString(body, 2 + topicLength, body.Length - 2 - topicLength);
        return (topic, payload);
    }

    private static byte[] ConnectPacket(
        string clientId,
        ushort keepAliveSeconds,
        (string Topic, string Payload)? will = null)
    {
        var clientIdBytes = Encoding.UTF8.GetBytes(clientId);
        var body = new List<byte>
        {
            0x00, 0x04, (byte)'M', (byte)'Q', (byte)'T', (byte)'T', // Protocol name "MQTT".
            0x04, // Protocol level 4 (MQTT 3.1.1).
            // Clean session plus the will flag when a will is supplied: Will QoS 0, no retain.
            will is null ? (byte)0x02 : (byte)0x06,
            (byte)(keepAliveSeconds >> 8), (byte)keepAliveSeconds,
            (byte)(clientIdBytes.Length >> 8), (byte)clientIdBytes.Length,
        };
        body.AddRange(clientIdBytes);

        if (will is { } message)
        {
            var topicBytes = Encoding.UTF8.GetBytes(message.Topic);
            var payloadBytes = Encoding.UTF8.GetBytes(message.Payload);
            body.Add((byte)(topicBytes.Length >> 8));
            body.Add((byte)topicBytes.Length);
            body.AddRange(topicBytes);
            body.Add((byte)(payloadBytes.Length >> 8));
            body.Add((byte)payloadBytes.Length);
            body.AddRange(payloadBytes);
        }

        return Packet(ConnectPacketType, [.. body]);
    }

    private static byte[] SubscribePacket(ushort packetId, string topicFilter, byte qos)
    {
        var topicBytes = Encoding.UTF8.GetBytes(topicFilter);
        var body = new List<byte>
        {
            (byte)(packetId >> 8), (byte)packetId,
            (byte)(topicBytes.Length >> 8), (byte)topicBytes.Length,
        };
        body.AddRange(topicBytes);
        body.Add(qos);
        return Packet(SubscribePacketType, [.. body]);
    }

    private static byte[] PublishPacket(string topic, ReadOnlySpan<byte> payload)
    {
        var topicBytes = Encoding.UTF8.GetBytes(topic);
        var body = new byte[2 + topicBytes.Length + payload.Length];
        body[0] = (byte)(topicBytes.Length >> 8);
        body[1] = (byte)topicBytes.Length;
        topicBytes.CopyTo(body, 2);
        payload.CopyTo(body.AsSpan(2 + topicBytes.Length));
        return Packet(PublishPacketType, body);
    }

    private static byte[] PingReqPacket() => [PingReqPacketType, 0x00];

    /// <summary>Builds a packet: fixed-header type byte, MQTT variable-length size, then the body.</summary>
    private static byte[] Packet(byte header, byte[] body)
    {
        var remaining = body.Length;
        var lengthBytes = new List<byte>(4);
        do
        {
            var encoded = (byte)(remaining % 128);
            remaining /= 128;
            if (remaining > 0)
            {
                encoded |= 0x80;
            }

            lengthBytes.Add(encoded);
        }
        while (remaining > 0);

        var packet = new byte[1 + lengthBytes.Count + body.Length];
        packet[0] = header;
        lengthBytes.CopyTo(packet, 1);
        body.CopyTo(packet, 1 + lengthBytes.Count);
        return packet;
    }

    private static async Task<(byte Header, byte[] Body)> ReadPacketAsync(
        NetworkStream stream,
        CancellationToken cancellationToken)
    {
        var header = new byte[1];
        await stream
            .ReadExactlyAsync(header, cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);

        var remaining = 0;
        var shift = 0;
        var lengthByte = new byte[1];
        do
        {
            await stream
                .ReadExactlyAsync(lengthByte, cancellationToken)
                .AsTask()
                .WaitAsync(StepTimeout, cancellationToken);
            remaining |= (lengthByte[0] & 0x7F) << shift;
            shift += 7;
        }
        while ((lengthByte[0] & 0x80) != 0);

        var body = new byte[remaining];
        await stream
            .ReadExactlyAsync(body, cancellationToken)
            .AsTask()
            .WaitAsync(StepTimeout, cancellationToken);
        return (header[0], body);
    }
}
