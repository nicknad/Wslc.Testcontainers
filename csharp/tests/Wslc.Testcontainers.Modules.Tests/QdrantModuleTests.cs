using System.Net.Http;
using Wslc.Testcontainers.Modules.Qdrant;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class QdrantModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_ports()
    {
        var container = new QdrantBuilder().Build();

        Assert.Equal("docker.io/qdrant/qdrant:v1.19.2", container.Image);
        Assert.Equal(6333, QdrantContainer.HttpPort);
        Assert.Equal(6334, QdrantContainer.GrpcPort);
        Assert.False(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_http_over_the_mapped_endpoint()
    {
        await using var qdrant = new QdrantBuilder().Build();

        await qdrant.StartAsync();

        var endpoint = qdrant.GetEndpoint();
        Assert.StartsWith("http://127.0.0.1:", endpoint, StringComparison.Ordinal);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var ready = await client.GetAsync(new Uri($"{endpoint}/readyz"), timeout.Token);
        Assert.True(ready.IsSuccessStatusCode);

        var collections = await client.GetAsync(new Uri($"{endpoint}/collections"), timeout.Token);
        Assert.True(collections.IsSuccessStatusCode);
    }
}
