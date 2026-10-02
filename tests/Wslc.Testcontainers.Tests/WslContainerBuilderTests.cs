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

        Assert.Equal(
            new[] { new WslPortMapping(8080, PortProtocol.TCP, null), new WslPortMapping(5432, PortProtocol.TCP, null) },
            builder.Build().Configuration.PortMappings);
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithPort(70000));
    }

    [Fact]
    public void WithPort_supports_udp_and_bind_addresses()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine")
            .WithPort(8080)
            .WithUdpPort(53)
            .WithPort(9090, PortProtocol.TCP, "127.0.0.1")
            .Build();

        Assert.Equal(
            new[]
            {
                new WslPortMapping(8080, PortProtocol.TCP, null),
                new WslPortMapping(53, PortProtocol.UDP, null),
                new WslPortMapping(9090, PortProtocol.TCP, "127.0.0.1"),
            },
            container.Configuration.PortMappings);
    }

    [Fact]
    public void Same_port_with_different_protocols_is_allowed()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine")
            .WithPort(8080, PortProtocol.TCP)
            .WithPort(8080, PortProtocol.UDP)
            .Build();

        Assert.Equal(2, container.Configuration.PortMappings.Count);
    }

    [Fact]
    public void WithPort_rejects_invalid_bind_addresses_and_conflicts()
    {
        var builder = new WslContainerBuilder().FromImage("alpine");
        Assert.Throws<ArgumentException>(() => builder.WithPort(8080, PortProtocol.TCP, "not-an-ip"));
        Assert.Throws<ArgumentException>(() => builder.WithPort(8080, PortProtocol.TCP, ""));

        var bound = builder.WithPort(8080, PortProtocol.TCP, "127.0.0.1");
        Assert.Throws<WslcException>(() => bound.WithPort(8080, PortProtocol.TCP, "0.0.0.0"));
    }

    [Fact]
    public void WithPort_rejects_unknown_protocols()
    {
        var builder = new WslContainerBuilder().FromImage("alpine");

        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithPort(8080, (PortProtocol)99));
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithPort(8080, (PortProtocol)99, "127.0.0.1"));
    }

    [Fact]
    public void WithPort_normalizes_the_bind_address()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine")
            .WithPort(8080, PortProtocol.TCP, "0:0:0:0:0:0:0:1")
            .Build();

        Assert.Equal("::1", Assert.Single(container.Configuration.PortMappings).BindAddress);
    }

    [Fact]
    public void WithCpuCount_and_WithMemoryMB_record_limits()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine")
            .WithCpuCount(2)
            .WithMemoryMB(2048)
            .Build();

        Assert.Equal(2u, container.Configuration.CpuCount);
        Assert.Equal(2048u, container.Configuration.MemorySizeInMB);
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithCpuCount(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithMemoryMB(0));
    }

    [Fact]
    public void WithNetworkingMode_None_rejects_ports_egress_and_network_waits()
    {
        var ports = new WslContainerBuilder().FromImage("alpine").WithPort(8080).WithNetworkingMode(ContainerNetworkingMode.None);
        Assert.Throws<WslcException>(() => ports.Build());

        var egress = new WslContainerBuilder().FromImage("alpine")
            .WithEgressAllowlist(new EgressAllowlistOptions())
            .WithNetworkingMode(ContainerNetworkingMode.None);
        Assert.Throws<WslcException>(() => egress.Build());

        var waits = new WslContainerBuilder().FromImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilTcpPortIsAvailable(80))
            .WithNetworkingMode(ContainerNetworkingMode.None);
        Assert.Throws<WslcException>(() => waits.Build());

        var composite = new WslContainerBuilder().FromImage("alpine")
            .WithWaitStrategy(
                Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilMessageIsLogged("ready")
                    .And(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilHttpRequestIsSucceeded("/health", 8080)))
            .WithNetworkingMode(ContainerNetworkingMode.None);
        Assert.Throws<WslcException>(() => composite.Build());

        // Non-network waits are fine without networking.
        var offline = new WslContainerBuilder().FromImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().UntilFileExists("/tmp/ready"))
            .WithNetworkingMode(ContainerNetworkingMode.None)
            .Build();
        Assert.Equal(ContainerNetworkingMode.None, offline.Configuration.NetworkingMode);
    }

    [Fact]
    public void WithNetworkingMode_rejects_unknown_values()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithNetworkingMode((ContainerNetworkingMode)99));
    }

    [Fact]
    public void WithNamedVolume_records_and_validates()
    {
        var container = new WslContainerBuilder()
            .FromImage("alpine")
            .WithNamedVolume("data", "/data", 10UL * 1024 * 1024 * 1024)
            .Build();

        var volume = Assert.Single(container.Configuration.NamedVolumes);
        Assert.Equal("data", volume.Name);
        Assert.Equal("/data", volume.ContainerPath);
        Assert.False(volume.ReadOnly);
        Assert.Equal(10UL * 1024 * 1024 * 1024, volume.SizeBytes);
        Assert.Equal(VhdType.Dynamic, volume.Type);

        var builder = new WslContainerBuilder().FromImage("alpine");
        Assert.Throws<ArgumentException>(() => builder.WithNamedVolume("", "/data", 100));
        Assert.Throws<ArgumentException>(() => builder.WithNamedVolume("a/b", "/data", 100));
        Assert.Throws<ArgumentException>(() => builder.WithNamedVolume("data", "relative", 100));
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithNamedVolume("data", "/data", 0));
        Assert.Throws<WslcException>(() => builder.WithNamedVolume("data", "/a", 100).WithNamedVolume("data", "/b", 100));
    }

    [Fact]
    public void WithEgressAllowlist_records_normalized_options()
    {
        var hosts = new List<string> { "10.1.2.3/8", "192.168.0.0/16", "10.0.0.0/8", "10.0.0.5" };
        var options = new EgressAllowlistOptions
        {
            AllowedHosts = hosts,
            AllowedTcpPorts = new[] { 443, 80, 443 },
        };
        var container = new WslContainerBuilder().FromImage("alpine").WithEgressAllowlist(options).Build();

        var recorded = container.Configuration.EgressAllowlist!;
        Assert.NotSame(options, recorded);
        Assert.Equal(new[] { "10.0.0.0/8", "10.0.0.5", "192.168.0.0/16" }, recorded.AllowedHosts);
        Assert.Equal(new[] { 80, 443 }, recorded.AllowedTcpPorts);

        // Mutating the caller's collections afterwards must not leak into the configuration.
        hosts.Add("8.8.8.8");
        Assert.Equal(3, recorded.AllowedHosts.Count);

        Assert.Throws<ArgumentNullException>(() => new WslContainerBuilder().WithEgressAllowlist(null!));
        Assert.Throws<ArgumentException>(() => new WslContainerBuilder().WithEgressAllowlist(new EgressAllowlistOptions { AllowedHosts = new[] { "example.com" } }));
        Assert.Throws<ArgumentException>(() => new WslContainerBuilder().WithEgressAllowlist(new EgressAllowlistOptions { AllowedHosts = new[] { "::1" } }));
        Assert.Throws<ArgumentOutOfRangeException>(() => new WslContainerBuilder().WithEgressAllowlist(new EgressAllowlistOptions { AllowedTcpPorts = new[] { 70000 } }));
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
