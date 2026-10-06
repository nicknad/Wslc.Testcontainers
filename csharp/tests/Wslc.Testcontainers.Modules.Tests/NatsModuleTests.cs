using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Modules.Nats;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class NatsModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new NatsBuilder().Build();

        Assert.Equal("docker.io/library/nats:2-alpine", container.Image);
        Assert.Equal(4222, NatsContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Credentials_require_both_values()
    {
        Assert.Throws<WslException>(() => new NatsBuilder().WithUsername("nats").Build());
        Assert.Throws<WslException>(() => new NatsBuilder().WithPassword("secret").Build());
    }

    [Fact]
    public void Builder_chains_jetstream_and_credentials()
    {
        var builder = new NatsBuilder();

        Assert.Same(builder, builder.WithJetStream());
        Assert.Same(builder, builder.WithUsername("nats"));
        Assert.Same(builder, builder.WithPassword("secret"));
    }

    [IntegrationFact]
    public async Task Starts_with_jetstream_and_announces_over_the_mapped_endpoint()
    {
        await using var nats = new NatsBuilder().WithJetStream().Build();

        await nats.StartAsync();

        var connectionString = nats.GetConnectionString();
        Assert.StartsWith("nats://127.0.0.1:", connectionString, StringComparison.Ordinal);
        var separator = connectionString.LastIndexOf(':');
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        using var client = new TcpClient();
        await client.ConnectAsync(
            connectionString["nats://".Length..separator],
            int.Parse(connectionString[(separator + 1)..], System.Globalization.CultureInfo.InvariantCulture),
            timeout.Token);
        using var stream = client.GetStream();

        var buffer = new byte[256];
        var read = await stream.ReadAsync(buffer, timeout.Token);

        Assert.StartsWith("INFO ", Encoding.ASCII.GetString(buffer, 0, read), StringComparison.Ordinal);
    }
}
