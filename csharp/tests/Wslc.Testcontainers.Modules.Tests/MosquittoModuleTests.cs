using System.Net;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Modules.Mosquitto;
using Wslc.Testcontainers.Testing;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class MosquittoModuleTests
{
    // MQTT 3.1.1 control packet type prefixes: CONNECT 0x10, CONNACK 0x20, PUBLISH 0x30
    // (QoS 0), SUBSCRIBE 0x82, SUBACK 0x90.
    private const byte ConnectPacketType = 0x10;
    private const byte ConnackPacketType = 0x20;
    private const byte PublishPacketType = 0x30;
    private const byte SubscribePacketType = 0x82;
    private const byte SubackPacketType = 0x90;

    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new MosquittoBuilder().Build();

        Assert.Equal("docker.io/library/eclipse-mosquitto:2.1.2", container.Image);
        Assert.Equal(1883, MosquittoContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new MosquittoBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/library/eclipse-mosquitto:2.0.18"));
        Assert.Same(builder, builder.WithWaitTimeout(TimeSpan.FromSeconds(30)));
        Assert.Same(builder, builder.WithReuse());
        Assert.Equal("docker.io/library/eclipse-mosquitto:2.0.18", builder.Build().Image);
    }

    [Fact]
    public void Renders_the_connection_string_for_a_supplied_endpoint()
    {
        var container = new MosquittoBuilder().Build();

        Assert.Equal(
            "mqtt://127.0.0.1:49153",
            container.GetConnectionString(new IPEndPoint(IPAddress.Loopback, 49153)));
        Assert.Equal(
            "mqtt://[::1]:49153",
            container.GetConnectionString(new IPEndPoint(IPAddress.IPv6Loopback, 49153)));
    }

    [IntegrationFact]
    public async Task Starts_and_routes_a_publish_from_one_connection_to_another()
    {
        await using var mosquitto = new MosquittoBuilder().Build();

        var cancellationToken = TestContext.Current.CancellationToken;
        await mosquitto.StartAsync(cancellationToken);

        Assert.StartsWith("mqtt://127.0.0.1:", mosquitto.GetConnectionString(), StringComparison.Ordinal);
        var endpoint = mosquitto.GetConnectEndpoint(MosquittoContainer.DefaultPort);

        using var subscriber = new TcpClient();
        await subscriber.ConnectAsync(endpoint.Address, endpoint.Port, cancellationToken);
        await using var subscriberStream = subscriber.GetStream();

        await subscriberStream.WriteAsync(ConnectPacket("wslc-subscriber"), cancellationToken);
        var connack = await ReadPacketAsync(subscriberStream, cancellationToken);
        Assert.Equal(ConnackPacketType, connack.Header);
        Assert.Equal(0, connack.Body[1]); // Session-present flag 0, connect return code 0 (accepted).

        await subscriberStream.WriteAsync(SubscribePacket(packetId: 1, "wslc/test", qos: 0), cancellationToken);
        var suback = await ReadPacketAsync(subscriberStream, cancellationToken);
        Assert.Equal(SubackPacketType, suback.Header);
        Assert.Equal(new byte[] { 0x00, 0x01, 0x00 }, suback.Body); // Packet id 1, granted QoS 0.

        using var publisher = new TcpClient();
        await publisher.ConnectAsync(endpoint.Address, endpoint.Port, cancellationToken);
        await using var publisherStream = publisher.GetStream();

        await publisherStream.WriteAsync(ConnectPacket("wslc-publisher"), cancellationToken);
        var publisherConnack = await ReadPacketAsync(publisherStream, cancellationToken);
        Assert.Equal(ConnackPacketType, publisherConnack.Header);
        Assert.Equal(0, publisherConnack.Body[1]);

        await publisherStream.WriteAsync(PublishPacket("wslc/test", "hello"u8), cancellationToken);

        var published = await ReadPacketAsync(subscriberStream, cancellationToken);
        Assert.Equal(PublishPacketType, published.Header);
        var topicLength = (published.Body[0] << 8) | published.Body[1];
        Assert.Equal("wslc/test", Encoding.UTF8.GetString(published.Body, 2, topicLength));
        Assert.Equal(
            "hello",
            Encoding.UTF8.GetString(published.Body, 2 + topicLength, published.Body.Length - 2 - topicLength));
    }

    private static byte[] ConnectPacket(string clientId)
    {
        var clientIdBytes = Encoding.UTF8.GetBytes(clientId);
        var body = new List<byte>
        {
            0x00, 0x04, (byte)'M', (byte)'Q', (byte)'T', (byte)'T', // Protocol name "MQTT".
            0x04, // Protocol level 4 (MQTT 3.1.1).
            0x02, // Clean session.
            0x00, 0x3C, // Keep-alive 60 seconds.
            (byte)(clientIdBytes.Length >> 8), (byte)clientIdBytes.Length,
        };
        body.AddRange(clientIdBytes);
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
        await stream.ReadExactlyAsync(header, cancellationToken);

        var remaining = 0;
        var shift = 0;
        var lengthByte = new byte[1];
        do
        {
            await stream.ReadExactlyAsync(lengthByte, cancellationToken);
            remaining |= (lengthByte[0] & 0x7F) << shift;
            shift += 7;
        }
        while ((lengthByte[0] & 0x80) != 0);

        var body = new byte[remaining];
        await stream.ReadExactlyAsync(body, cancellationToken);
        return (header[0], body);
    }
}
