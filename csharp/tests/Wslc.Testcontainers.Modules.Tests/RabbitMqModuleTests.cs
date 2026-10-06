using System.Net.Sockets;
using Wslc.Testcontainers.Modules.RabbitMq;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class RabbitMqModuleTests
{
    private static readonly byte[] AmqpProtocolHeader = [(byte)'A', (byte)'M', (byte)'Q', (byte)'P', 0, 0, 9, 1];

    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new RabbitMqBuilder().Build();

        Assert.Equal("docker.io/library/rabbitmq:4-alpine", container.Image);
        Assert.Equal(5672, RabbitMqContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new RabbitMqBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/library/rabbitmq:4.1-alpine"));
        Assert.Equal("docker.io/library/rabbitmq:4.1-alpine", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_negotiates_amqp_over_the_mapped_endpoint()
    {
        await using var rabbitmq = new RabbitMqBuilder().Build();

        await rabbitmq.StartAsync();

        Assert.StartsWith("amqp://rabbit:secret@127.0.0.1:", rabbitmq.GetConnectionString(), StringComparison.Ordinal);
        var endpoint = rabbitmq.GetConnectEndpoint(RabbitMqContainer.DefaultPort);
        using var client = new TcpClient();
        await client.ConnectAsync(endpoint.Address, endpoint.Port);
        using var stream = client.GetStream();

        await stream.WriteAsync(AmqpProtocolHeader);
        // AMQP 0-9-1: a server that accepts the protocol header replies with a method frame
        // (type 1, channel 0) carrying Connection.Start (class 10, method 10) rather than
        // echoing the header.
        var frameHeader = new byte[7];
        await stream.ReadExactlyAsync(frameHeader);
        var payloadSize = (frameHeader[3] << 24) | (frameHeader[4] << 16) | (frameHeader[5] << 8) | frameHeader[6];
        var payload = new byte[payloadSize];
        await stream.ReadExactlyAsync(payload);

        Assert.Equal(1, frameHeader[0]);
        Assert.Equal(0, frameHeader[1]);
        Assert.Equal(0, frameHeader[2]);
        Assert.Equal(10, (payload[0] << 8) | payload[1]);
        Assert.Equal(10, (payload[2] << 8) | payload[3]);
    }
}
