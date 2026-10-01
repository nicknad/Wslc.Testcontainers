using Wslc.Testcontainers.Networking;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslContainerBuilderTests
{
    [Fact]
    public void Build_requires_an_image_source()
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void Build_rejects_a_missing_tarball()
    {
        var builder = new WslContainerBuilder().FromTarball("does-not-exist.tar");

        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void FromImage_records_the_image()
    {
        var container = new WslContainerBuilder().FromImage("alpine:latest").Build();

        Assert.Equal("alpine:latest", container.Image);
        Assert.Equal("alpine:latest", container.Configuration.Image);
        Assert.StartsWith("wslc-", container.Name, StringComparison.Ordinal);
    }

    [Fact]
    public void FromTarball_records_the_tarball_and_optional_image_name()
    {
        var path = Path.Combine(Path.GetTempPath(), $"wslc-{Guid.NewGuid():N}.tar");
        File.WriteAllText(path, "not-a-real-tarball");
        try
        {
            var container = new WslContainerBuilder().FromTarball(path, "custom:local").Build();

            Assert.Equal(path, container.Configuration.TarballPath);
            Assert.Equal("custom:local", container.Configuration.TarballImageName);
            Assert.Null(container.Configuration.Image);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Builders_are_immutable()
    {
        var baseBuilder = new WslContainerBuilder()
            .FromImage("alpine:latest")
            .WithEnvironment("A", "1");

        var withCommand = baseBuilder.WithCommand("redis-server");
        var withOtherCommand = baseBuilder.WithCommand("nginx");

        Assert.Null(baseBuilder.Build().Configuration.Command);
        Assert.Equal("redis-server", withCommand.Build().Configuration.Command);
        Assert.Equal("nginx", withOtherCommand.Build().Configuration.Command);
    }

    [Fact]
    public void WithPort_validates_and_deduplicates()
    {
        var builder = new WslContainerBuilder().FromImage("alpine").WithPort(8080).WithPort(8080).WithPort(5432);

        Assert.Equal(new[] { 8080, 5432 }, builder.Build().Configuration.Ports);
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(70000));
    }

    [Theory]
    [InlineData("")]
    [InlineData("1INVALID")]
    [InlineData("HAS-DASH")]
    public void WithEnvironment_rejects_invalid_names(string name)
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<ArgumentException>(() => builder.WithEnvironment(name, "value"));
    }

    [Fact]
    public void WithFile_requires_an_existing_file()
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<WslcException>(() => builder.WithFile("missing.txt", "/tmp/missing.txt"));
    }

    [Fact]
    public void WithVolume_requires_an_existing_directory()
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<WslcException>(() => builder.WithVolume("missing-directory", "/data"));
    }

    [Fact]
    public async Task Container_guards_access_before_start()
    {
        await using var container = new WslContainerBuilder().FromImage("alpine:latest").Build();

        Assert.False(container.IsStarted);
        Assert.Throws<WslNetworkException>(() => container.GetMappedPort(8080));
        await Assert.ThrowsAsync<InvalidOperationException>(() => container.ExecAsync("echo"));
    }

    [Fact]
    public async Task Dispose_is_safe_for_unstarted_containers()
    {
        var container = new WslContainerBuilder().FromImage("alpine:latest").Build();

        await container.DisposeAsync();
        await container.DisposeAsync();
    }

    [Fact]
    public void Wait_strategies_accumulate()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine:latest")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilTcpPortIsAvailable(80))
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilProcessIsRunning("nginx"))
            .Build();

        Assert.Equal(2, container.Configuration.WaitStrategies.Count);
    }
}
