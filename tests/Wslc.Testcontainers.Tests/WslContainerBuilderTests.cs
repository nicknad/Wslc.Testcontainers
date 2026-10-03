using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Networking;
using Wslc.Testcontainers.Provisioning;
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
    public void WithImage_records_the_image()
    {
        var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

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
    public void Builders_mutate_in_place_and_build_snapshots_configuration()
    {
        var builder = new WslContainerBuilder()
            .WithImage("alpine:latest")
            .WithEnvironment("A", "1");

        var container = builder.Build();
        builder.WithCommand("redis-server");

        Assert.Null(container.Configuration.Command);
        Assert.Equal("redis-server", builder.Build().Configuration.Command);
        Assert.Same(builder, builder.WithCommand("nginx"));
        Assert.Equal("nginx", builder.Build().Configuration.Command);
    }

    [Fact]
    public void WithPort_validates_and_deduplicates()
    {
        var builder = new WslContainerBuilder().WithImage("alpine").WithPort(8080).WithPort(8080).WithPort(5432);

        Assert.Equal(
            new[] { new WslPortMapping(8080, null), new WslPortMapping(5432, null) },
            builder.Build().Configuration.PortMappings);
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(70000));
    }

    [Fact]
    public void WithPort_supports_bind_addresses()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithPort(8080)
            .WithPort(9090, "127.0.0.1")
            .Build();

        Assert.Equal(
            new[]
            {
                new WslPortMapping(8080, null),
                new WslPortMapping(9090, "127.0.0.1"),
            },
            container.Configuration.PortMappings);
    }

    [Fact]
    public void WithPort_rejects_invalid_bind_addresses_and_conflicts()
    {
        var builder = new WslContainerBuilder().WithImage("alpine");
        Assert.Throws<ArgumentException>(() => builder.WithPort(8080, "not-an-ip"));
        Assert.Throws<ArgumentException>(() => builder.WithPort(8080, ""));

        var bound = builder.WithPort(8080, "127.0.0.1");
        Assert.Throws<WslcException>(() => bound.WithPort(8080, "0.0.0.0"));
    }

    [Fact]
    public void WithPort_normalizes_the_bind_address()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithPort(8080, "0:0:0:0:0:0:0:1")
            .Build();

        Assert.Equal("::1", Assert.Single(container.Configuration.PortMappings).BindAddress);
    }

    [Fact]
    public void WithCpuCount_and_WithMemoryMB_record_limits()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithCpuCount(2)
            .WithMemoryMB(2048)
            .Build();

        Assert.Equal(2u, container.Configuration.CpuCount);
        Assert.Equal(2048u, container.Configuration.MemorySizeInMB);
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithCpuCount(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithMemoryMB(0));
    }

    [Fact]
    public void WithNetworkingMode_None_rejects_ports_and_network_waits()
    {
        var ports = new WslContainerBuilder().WithImage("alpine").WithPort(8080).WithNetworkingMode(ContainerNetworkMode.None);
        Assert.Throws<WslcException>(() => ports.Build());

        var waits = new WslContainerBuilder().WithImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilTcpPortIsAvailable(80))
            .WithNetworkingMode(ContainerNetworkMode.None);
        Assert.Throws<WslcException>(() => waits.Build());

        var composite = new WslContainerBuilder().WithImage("alpine")
            .WithWaitStrategy(
                Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilMessageIsLogged("ready")
                    .And(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilHttpRequestIsSucceeded("/health", 8080)))
            .WithNetworkingMode(ContainerNetworkMode.None);
        Assert.Throws<WslcException>(() => composite.Build());

        // Non-network waits are fine without networking.
        var offline = new WslContainerBuilder().WithImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilFileExists("/tmp/ready"))
            .WithNetworkingMode(ContainerNetworkMode.None)
            .Build();
        Assert.Equal(ContainerNetworkMode.None, offline.Configuration.NetworkingMode);
    }

    [Fact]
    public void WithNetworkingMode_rejects_unknown_values()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithNetworkingMode((ContainerNetworkMode)99));
    }

    [Fact]
    public void WithSessionVolume_records_and_validates()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithSessionVolume("data", "/data", 10UL * 1024 * 1024 * 1024)
            .Build();

        var volume = Assert.Single(container.Configuration.SessionVolumes);
        Assert.Equal("data", volume.Name);
        Assert.Equal("/data", volume.ContainerPath);
        Assert.False(volume.ReadOnly);
        Assert.Equal(10UL * 1024 * 1024 * 1024, volume.SizeBytes);
        Assert.Equal(VhdAllocationType.Dynamic, volume.Type);

        var builder = new WslContainerBuilder().WithImage("alpine");
        Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("", "/data", 100));
        Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("a/b", "/data", 100));
        Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("a b", "/data", 100));
        Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("data", "relative", 100));
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithSessionVolume("data", "/data", 0));
        Assert.Throws<WslcException>(() => builder.WithSessionVolume("data", "/a", 100).WithSessionVolume("data", "/b", 100));
        Assert.Throws<WslcException>(() => builder.WithSessionVolume("Data", "/a", 100).WithSessionVolume("data", "/b", 100));

        var readOnlyFixed = new WslContainerBuilder()
            .WithImage("alpine")
            .WithSessionVolume("data", "/data", 100, VolumeAccess.ReadOnly, VhdAllocationType.Fixed)
            .Build();
        var configured = Assert.Single(readOnlyFixed.Configuration.SessionVolumes);
        Assert.True(configured.ReadOnly);
        Assert.Equal(VhdAllocationType.Fixed, configured.Type);
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
        await using var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        Assert.False(container.IsStarted);
        Assert.Throws<WslcException>(() => container.GetMappedPort(8080));
        await Assert.ThrowsAsync<WslcException>(() => container.ExecAsync("echo"));
    }

    [Fact]
    public async Task Dispose_is_safe_for_unstarted_containers()
    {
        var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        await container.DisposeAsync();
        await container.DisposeAsync();
    }

    [Fact]
    public void Wait_strategies_accumulate()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine:latest")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilTcpPortIsAvailable(80))
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilProcessIsRunning("nginx"))
            .Build();

        Assert.Equal(2, container.Configuration.WaitStrategies.Count);
    }

    [Fact]
    public void Network_waits_accept_non_loopback_bind_addresses()
    {
        // Readiness probes honor the configured bind address, so non-loopback bindings
        // are valid with TCP and HTTP waits (no build-time rejection, no guaranteed timeout).
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithPort(8080, "192.168.1.10")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilTcpPortIsAvailable(8080))
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilHttpRequestIsSucceeded("/health", 8080))
            .Build();

        Assert.Equal(2, container.Configuration.WaitStrategies.Count);
        Assert.Equal("192.168.1.10", Assert.Single(container.Configuration.PortMappings).BindAddress);
    }
}
