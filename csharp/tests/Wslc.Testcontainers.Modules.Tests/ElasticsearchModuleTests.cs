using System.Net.Http;
using Wslc.Testcontainers.Modules.Elasticsearch;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class ElasticsearchModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new ElasticsearchBuilder().Build();

        Assert.Equal("docker.io/library/elasticsearch:9.5.3", container.Image);
        Assert.Equal(9200, ElasticsearchContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Starts_and_answers_rest_over_the_mapped_endpoint()
    {
        await using var elasticsearch = new ElasticsearchBuilder().Build();

        await elasticsearch.StartAsync(TestContext.Current.CancellationToken);

        Assert.StartsWith("http://127.0.0.1:", elasticsearch.GetEndpoint(), StringComparison.Ordinal);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(2));
        using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(2) };

        using var root = await client.GetAsync(new Uri($"{elasticsearch.GetEndpoint()}/"), timeout.Token);
        Assert.True(root.IsSuccessStatusCode);

        using var health = await client.GetAsync(new Uri($"{elasticsearch.GetEndpoint()}/_cluster/health"), timeout.Token);
        Assert.True(health.IsSuccessStatusCode);
    }
}
