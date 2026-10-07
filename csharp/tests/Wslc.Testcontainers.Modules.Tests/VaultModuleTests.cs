using System.Net.Http;
using Wslc.Testcontainers.Modules.Tests.Support;
using Wslc.Testcontainers.Modules.Vault;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class VaultModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new VaultBuilder().Build();

        Assert.Equal("docker.io/hashicorp/vault:2.1", container.Image);
        Assert.Equal(8200, VaultContainer.DefaultPort);
        Assert.Equal("root", container.RootToken);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_root_token_override()
    {
        var builder = new VaultBuilder();

        Assert.Same(builder, builder.WithRootToken("test-token"));

        var container = builder.Build();
        Assert.Equal("test-token", container.RootToken);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_the_health_endpoint()
    {
        await using var vault = new VaultBuilder().Build();

        await vault.StartAsync();

        Assert.StartsWith("http://127.0.0.1:", vault.GetAddress(), StringComparison.Ordinal);
        Assert.Equal("root", vault.RootToken);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var health = await client.GetAsync(new Uri($"{vault.GetAddress()}/v1/sys/health"), timeout.Token);

        Assert.True(health.IsSuccessStatusCode);

        var result = await vault.ExecAsync(
            "/bin/sh",
            ["-c", "VAULT_ADDR=http://127.0.0.1:8200 VAULT_TOKEN=root vault kv put secret/wslc value=1"],
            new ExecOptions { Timeout = TimeSpan.FromSeconds(60) },
            TestContext.Current.CancellationToken);

        Assert.Equal(0, result.ExitCode);
    }
}
