using System.Buffers.Binary;
using System.Net.Sockets;
using Wslc.Testcontainers.Modules.Kafka;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class KafkaModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new KafkaBuilder().Build();

        Assert.Equal("docker.io/apache/kafka:4.3.1", container.Image);
        Assert.Equal(9092, KafkaContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new KafkaBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/apache/kafka:4.3.1"));
        Assert.Equal("docker.io/apache/kafka:4.3.1", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_metadata_on_the_advertised_host_port()
    {
        await using var kafka = new KafkaBuilder().Build();

        await kafka.StartAsync();

        var bootstrap = kafka.GetBootstrapServers();
        Assert.StartsWith("127.0.0.1:", bootstrap, StringComparison.Ordinal);

        // Kafka advertises the fixed host port, so a host-side client must reach the broker at the
        // same endpoint. Ask for the supported API versions: a live broker answers with the request
        // correlation id and error code 0 (the container-internal CLI cannot do this because the
        // advertised listener points at the Windows host, not the container).
        var endpoint = kafka.GetConnectEndpoint(KafkaContainer.DefaultPort);
        using var client = new TcpClient();
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        await client.ConnectAsync(endpoint.Address, endpoint.Port, cancellation.Token);
        using var stream = client.GetStream();

        // ApiVersions v0: length, apiKey=18, apiVersion=0, correlationId=1, null clientId.
        await stream.WriteAsync(new byte[] { 0, 0, 0, 10, 0, 18, 0, 0, 0, 0, 0, 1, 0xFF, 0xFF }, cancellation.Token);
        var lengthBytes = new byte[4];
        await stream.ReadExactlyAsync(lengthBytes, cancellation.Token);
        var length = BinaryPrimitives.ReadInt32BigEndian(lengthBytes);
        Assert.InRange(length, 4, 16 * 1024 * 1024);
        var payload = new byte[length];
        await stream.ReadExactlyAsync(payload, cancellation.Token);
        Assert.Equal(1, BinaryPrimitives.ReadInt32BigEndian(payload));
        Assert.Equal(0, BinaryPrimitives.ReadInt16BigEndian(payload.AsSpan(4)));
    }
}
