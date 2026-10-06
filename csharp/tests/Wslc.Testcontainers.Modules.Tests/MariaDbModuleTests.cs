using Wslc.Testcontainers.Modules.MariaDb;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class MariaDbModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new MariaDbBuilder().Build();

        Assert.Equal("docker.io/library/mariadb:11.4", container.Image);
        Assert.Equal(3306, MariaDbContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new MariaDbBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/library/mariadb:11.8"));
        Assert.Equal("docker.io/library/mariadb:11.8", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_select_over_the_mapped_endpoint()
    {
        await using var mariadb = new MariaDbBuilder().Build();

        await mariadb.StartAsync();

        Assert.StartsWith("Server=127.0.0.1;", mariadb.GetConnectionString(), StringComparison.Ordinal);
        var result = await mariadb.ExecAsync(
            "mariadb",
            ["-u", "mariadb", "--password=secret", "-D", "customers", "-e", "SELECT 1"]);

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("1", result.Stdout, StringComparison.Ordinal);
    }
}
