using System.Net.Http;
using Wslc.Testcontainers.Modules.Azurite;
using Wslc.Testcontainers.Testing;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class AzuriteModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_ports_and_account()
    {
        var container = new AzuriteBuilder().Build();

        Assert.Equal("mcr.microsoft.com/azure-storage/azurite:3.37.0", container.Image);
        Assert.Equal(10000, AzuriteContainer.BlobPort);
        Assert.Equal(10001, AzuriteContainer.QueuePort);
        Assert.Equal(10002, AzuriteContainer.TablePort);
        Assert.Equal("devstoreaccount1", AzuriteContainer.AccountName);
        Assert.False(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_all_three_storage_services()
    {
        await using var azurite = new AzuriteBuilder().Build();

        await azurite.StartAsync(TestContext.Current.CancellationToken);

        var blob = azurite.GetBlobEndpoint();
        var queue = azurite.GetQueueEndpoint();
        var table = azurite.GetTableEndpoint();
        Assert.StartsWith("http://127.0.0.1:", blob, StringComparison.Ordinal);
        Assert.EndsWith($"/{AzuriteContainer.AccountName}", blob, StringComparison.Ordinal);
        Assert.EndsWith($"/{AzuriteContainer.AccountName}", queue, StringComparison.Ordinal);
        Assert.EndsWith($"/{AzuriteContainer.AccountName}", table, StringComparison.Ordinal);

        var connectionString = azurite.GetConnectionString();
        Assert.Contains($"BlobEndpoint={blob};", connectionString, StringComparison.Ordinal);
        Assert.Contains($"QueueEndpoint={queue};", connectionString, StringComparison.Ordinal);
        Assert.Contains($"TableEndpoint={table};", connectionString, StringComparison.Ordinal);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };

        // Azurite requires SharedKey authentication, so an unauthenticated request is rejected
        // with a 4xx status. Any non-5xx response proves the service is listening and serving.
        foreach (var endpoint in new[] { blob, queue, table })
        {
            using var response = await client.GetAsync(new Uri(endpoint), timeout.Token);
            Assert.True((int)response.StatusCode < 500, $"{endpoint} returned {(int)response.StatusCode}.");
        }
    }
}
