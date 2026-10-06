using Wslc.Testcontainers.Modules.MongoDb;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class MongoDbModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_port()
    {
        var container = new MongoDbBuilder().Build();

        Assert.Equal("docker.io/library/mongo:8", container.Image);
        Assert.Equal(27017, MongoDbContainer.DefaultPort);
        Assert.False(container.IsStarted);
    }

    [Fact]
    public void Credentials_require_both_values()
    {
        Assert.Throws<WslException>(() => new MongoDbBuilder().WithUsername("root").Build());
        Assert.Throws<WslException>(() => new MongoDbBuilder().WithPassword("secret").Build());
    }

    [IntegrationFact]
    public async Task Starts_without_auth_and_answers_ping()
    {
        await using var mongodb = new MongoDbBuilder().Build();

        await mongodb.StartAsync();

        Assert.StartsWith("mongodb://127.0.0.1:", mongodb.GetConnectionString(), StringComparison.Ordinal);
        var result = await mongodb.ExecAsync(
            "mongosh",
            ["--quiet", "--eval", "db.adminCommand({ping:1}).ok"],
            new ExecOptions { Timeout = TimeSpan.FromSeconds(60) },
            TestContext.Current.CancellationToken);

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("1", result.Stdout, StringComparison.Ordinal);
    }

    [IntegrationFact]
    public async Task Starts_with_root_credentials_and_answers_ping()
    {
        await using var mongodb = new MongoDbBuilder()
            .WithUsername("root")
            .WithPassword("secret")
            .Build();

        await mongodb.StartAsync();

        Assert.Contains("authSource=admin", mongodb.GetConnectionString(), StringComparison.Ordinal);
        var result = await mongodb.ExecAsync(
            "mongosh",
            [
                "--quiet",
                "--username", "root",
                "--password", "secret",
                "--authenticationDatabase", "admin",
                "--eval", "db.adminCommand({ping:1}).ok",
            ],
            new ExecOptions { Timeout = TimeSpan.FromSeconds(60) },
            TestContext.Current.CancellationToken);

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("1", result.Stdout, StringComparison.Ordinal);
    }
}
