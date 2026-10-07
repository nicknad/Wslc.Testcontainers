using System.Net.Http;
using Wslc.Testcontainers.Modules.ClickHouse;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class ClickHouseModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_ports()
    {
        var container = new ClickHouseBuilder().Build();

        Assert.Equal("docker.io/clickhouse/clickhouse-server:26.7", container.Image);
        Assert.Equal(8123, ClickHouseContainer.HttpPort);
        Assert.Equal(9000, ClickHouseContainer.NativePort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains()
    {
        var builder = new ClickHouseBuilder();

        Assert.Same(builder, builder.WithUsername("demo"));
        Assert.Same(builder, builder.WithPassword("secret"));
        Assert.Same(builder, builder.WithDatabase("demo"));
    }

    [IntegrationFact]
    public async Task Starts_with_default_credentials_and_serves_http_and_queries()
    {
        await using var clickhouse = new ClickHouseBuilder().Build();

        await clickhouse.StartAsync();

        Assert.StartsWith("Host=127.0.0.1;Port=", clickhouse.GetConnectionString(), StringComparison.Ordinal);
        var endpoint = clickhouse.GetConnectEndpoint(ClickHouseContainer.HttpPort);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var ping = await client.GetAsync(new Uri($"http://{endpoint.Address}:{endpoint.Port}/ping"), timeout.Token);

        Assert.True(ping.IsSuccessStatusCode);
        Assert.Equal("Ok.", (await ping.Content.ReadAsStringAsync(timeout.Token)).Trim());

        var result = await clickhouse.ExecAsync(
            "clickhouse-client",
            ["--query", "SELECT 1"],
            new ExecOptions { Timeout = TimeSpan.FromSeconds(60) },
            TestContext.Current.CancellationToken);

        Assert.Equal(0, result.ExitCode);
        Assert.Equal("1", result.Stdout.Trim());
    }
}
