using Wslc.Testcontainers.Modules.SqlServer;
using Wslc.Testcontainers.Testing;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class SqlServerModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new SqlServerBuilder().Build();

        Assert.Equal("mcr.microsoft.com/mssql/server:2022-latest", container.Image);
        Assert.Equal(1433, SqlServerContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Builder_chains_and_honors_the_image_override()
    {
        var builder = new SqlServerBuilder();

        Assert.Same(builder, builder.WithImage("mcr.microsoft.com/mssql/server:2025-latest"));
        Assert.Same(builder, builder.WithPassword("Shared!Password1"));
        Assert.Equal("mcr.microsoft.com/mssql/server:2025-latest", builder.Build().Image);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_select_over_the_mapped_endpoint()
    {
        await using var sqlServer = new SqlServerBuilder().Build();

        var cancellationToken = TestContext.Current.CancellationToken;
        await sqlServer.StartAsync(cancellationToken);

        Assert.StartsWith("Server=127.0.0.1,", sqlServer.GetConnectionString(), StringComparison.Ordinal);
        var result = await sqlServer.ExecAsync(
            "/opt/mssql-tools18/bin/sqlcmd",
            ["-S", "localhost", "-U", "sa", "-C", "-b", "-Q", "SELECT 1"],
            new ExecOptions
            {
                Environment = new Dictionary<string, string> { ["SQLCMDPASSWORD"] = "yourStrong(!)Password" },
                Timeout = TimeSpan.FromSeconds(60),
            },
            cancellationToken);

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("1", result.Stdout, StringComparison.Ordinal);
    }
}
