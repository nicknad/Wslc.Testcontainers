using System.Globalization;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Testing;
using Wslc.Testcontainers.Modules.Valkey;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class ValkeyModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new ValkeyBuilder().Build();

        Assert.Equal("docker.io/valkey/valkey:8-alpine", container.Image);
        Assert.Equal(6379, ValkeyContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new ValkeyBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/valkey/valkey:9-alpine"));
        Assert.Equal("docker.io/valkey/valkey:9-alpine", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_answers_ping_over_the_mapped_endpoint()
    {
        await using var valkey = new ValkeyBuilder().Build();

        var cancellationToken = TestContext.Current.CancellationToken;
        await valkey.StartAsync(cancellationToken);

        var endpoint = valkey.GetEndpoint();
        var separator = endpoint.LastIndexOf(':');
        using var client = new TcpClient();
        await client.ConnectAsync(
            endpoint[..separator],
            int.Parse(endpoint[(separator + 1)..], CultureInfo.InvariantCulture),
            cancellationToken);
        using var stream = client.GetStream();

        await stream.WriteAsync(Encoding.ASCII.GetBytes("PING\r\n"), cancellationToken);
        var buffer = new byte[32];
        var read = await stream.ReadAsync(buffer, cancellationToken);

        Assert.StartsWith("+PONG", Encoding.ASCII.GetString(buffer, 0, read), StringComparison.Ordinal);
    }
}
