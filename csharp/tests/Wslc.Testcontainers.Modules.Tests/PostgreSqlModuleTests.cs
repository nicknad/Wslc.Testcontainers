using Wslc.Testcontainers.Modules.PostgreSql;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class PostgreSqlModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new PostgreSqlBuilder().Build();

        Assert.Equal("docker.io/library/postgres:15-alpine", container.Image);
        Assert.Equal(5432, PostgreSqlContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new PostgreSqlBuilder();

        Assert.Same(builder, builder.WithImage("docker.io/library/postgres:16-alpine"));
        Assert.Same(builder, builder.WithUsername("admin"));
        Assert.Same(builder, builder.WithPassword("hunter2"));
        Assert.Same(builder, builder.WithDatabase("app"));
        Assert.Equal("docker.io/library/postgres:16-alpine", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_select_over_the_mapped_endpoint()
    {
        await using var postgres = new PostgreSqlBuilder().Build();

        await postgres.StartAsync();

        Assert.StartsWith("Host=127.0.0.1;", postgres.GetConnectionString(), StringComparison.Ordinal);
        var result = await postgres.ExecAsync(
            "psql",
            ["-U", "postgres", "-d", "customers", "-tAc", "SELECT 1"],
            new ExecOptions
            {
                Environment = new Dictionary<string, string> { ["PGPASSWORD"] = "secret" },
                Timeout = TimeSpan.FromSeconds(60),
            },
            TestContext.Current.CancellationToken);

        Assert.Equal(0, result.ExitCode);
        Assert.Equal("1", result.Stdout.Trim());
    }
}
