using System.Net.Http;
using Wslc.Testcontainers.Modules.WireMock;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class WireMockModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new WireMockBuilder().Build();

        Assert.Equal("docker.io/wiremock/wiremock:3x", container.Image);
        Assert.Equal(8080, WireMockContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Starts_and_answers_health_and_admin_mappings_over_the_mapped_endpoint()
    {
        await using var wiremock = new WireMockBuilder().Build();

        await wiremock.StartAsync();

        Assert.StartsWith("http://127.0.0.1:", wiremock.GetEndpoint(), StringComparison.Ordinal);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };

        var health = await client.GetAsync(new Uri($"{wiremock.GetEndpoint()}/__admin/health"), timeout.Token);
        Assert.True(health.IsSuccessStatusCode);

        var mappings = await client.GetAsync(new Uri($"{wiremock.GetEndpoint()}/__admin/mappings"), timeout.Token);
        Assert.True(mappings.IsSuccessStatusCode);
    }
}
