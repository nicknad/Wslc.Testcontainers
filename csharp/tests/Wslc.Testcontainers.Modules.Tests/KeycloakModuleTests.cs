using System.Net.Http;
using Wslc.Testcontainers.Modules.Keycloak;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class KeycloakModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_port_and_credentials()
    {
        var container = new KeycloakBuilder().Build();

        Assert.Equal("docker.io/keycloak/keycloak:26.8", container.Image);
        Assert.Equal(8080, KeycloakContainer.DefaultPort);
        Assert.Equal("admin", container.AdminUsername);
        Assert.Equal("admin", container.AdminPassword);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_credential_overrides()
    {
        var builder = new KeycloakBuilder();

        Assert.Same(builder, builder.WithAdminUsername("keycloak-user"));
        Assert.Same(builder, builder.WithAdminPassword("s3cret"));

        var container = builder.Build();
        Assert.Equal("keycloak-user", container.AdminUsername);
        Assert.Equal("s3cret", container.AdminPassword);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_the_master_realm_endpoint()
    {
        await using var keycloak = new KeycloakBuilder()
            .WithWaitTimeout(TimeSpan.FromMinutes(3))
            .Build();

        await keycloak.StartAsync();

        Assert.StartsWith("http://127.0.0.1:", keycloak.GetEndpoint(), StringComparison.Ordinal);
        Assert.Equal("admin", keycloak.AdminUsername);
        Assert.Equal("admin", keycloak.AdminPassword);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var response = await client.GetAsync(new Uri($"{keycloak.GetEndpoint()}/realms/master"), timeout.Token);

        Assert.True(response.IsSuccessStatusCode);
        var body = await response.Content.ReadAsStringAsync(timeout.Token);
        Assert.Contains("\"master\"", body, StringComparison.Ordinal);
    }
}
