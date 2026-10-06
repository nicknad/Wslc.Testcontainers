using System.Net.Http;
using Wslc.Testcontainers.Modules.RustFs;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class RustFsModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_ports_and_credentials()
    {
        var container = new RustFsBuilder().Build();

        Assert.Equal("docker.io/rustfs/rustfs:1.0.1", container.Image);
        Assert.Equal(9000, RustFsContainer.S3Port);
        Assert.Equal(9001, RustFsContainer.ConsolePort);
        Assert.Equal("rustfsadmin", container.AccessKey);
        Assert.Equal("rustfsadmin", container.SecretKey);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_credential_overrides()
    {
        var builder = new RustFsBuilder();

        Assert.Same(builder, builder.WithAccessKey("testkey"));
        Assert.Same(builder, builder.WithSecretKey("testsecret"));

        var container = builder.Build();
        Assert.Equal("testkey", container.AccessKey);
        Assert.Equal("testsecret", container.SecretKey);
    }

    [IntegrationFact]
    public async Task Starts_and_answers_health_over_the_mapped_endpoint()
    {
        await using var rustfs = new RustFsBuilder().Build();

        await rustfs.StartAsync();

        Assert.StartsWith("http://127.0.0.1:", rustfs.GetEndpoint(), StringComparison.Ordinal);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var health = await client.GetAsync(new Uri($"{rustfs.GetEndpoint()}/health"), TestContext.Current.CancellationToken);

        Assert.True(health.IsSuccessStatusCode);
    }
}
