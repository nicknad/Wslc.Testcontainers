using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Internal;
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
    public void FromTarball_rejects_a_missing_tarball()
    {
        Assert.Throws<WslcException>(() => new WslContainerBuilder().FromTarball("does-not-exist.tar"));
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

        // Detection covers only built-in TCP/HTTP waits; a custom condition is not inspected and
        // can still be combined with None (documented bypass).
        var custom = new WslContainerBuilder().WithImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().Until("custom", (_, _) => Task.FromResult(true)))
            .WithNetworkingMode(ContainerNetworkMode.None)
            .Build();
        Assert.Equal(ContainerNetworkMode.None, custom.Configuration.NetworkingMode);

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

    [Theory]
    [InlineData("café")]
    [InlineData("Ωmega")]
    [InlineData("Aé")]
    public void WithEnvironment_rejects_non_ascii_names(string name)
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<ArgumentException>(() => builder.WithEnvironment(name, "value"));
    }

    [Theory]
    [InlineData("A_B")]
    [InlineData("_x1")]
    public void WithEnvironment_accepts_ascii_names(string name)
    {
        var container = new WslContainerBuilder().WithImage("alpine").WithEnvironment(name, "value").Build();

        Assert.Equal("value", container.Configuration.Environment[name]);
    }

    [Theory]
    [InlineData("café")]
    [InlineData("Ωmega")]
    [InlineData("Aé")]
    public async Task Exec_rejects_non_ascii_environment_names(string name)
    {
        await using var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        await Assert.ThrowsAsync<ArgumentException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Environment = new Dictionary<string, string> { [name] = "value" } },
                CancellationToken.None));
    }

    [Fact]
    public void WithFile_requires_an_existing_file()
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<WslcException>(() => builder.WithFile("missing.txt", "/tmp/missing.txt"));
    }

    [Fact]
    public void WithFile_stores_an_absolute_source_at_build_time()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-absolutize-file");
        var original = Directory.GetCurrentDirectory();
        try
        {
            var file = Path.Combine(directory.FullName, "payload.txt");
            File.WriteAllText(file, "payload");
            Directory.SetCurrentDirectory(directory.FullName);

            var container = new WslContainerBuilder()
                .WithImage("alpine")
                .WithFile("payload.txt", "/tmp/payload.txt")
                .Build();

            var copy = Assert.Single(container.Configuration.Files);
            Assert.True(Path.IsPathFullyQualified(copy.Source));
            Assert.Equal(Path.GetFullPath(file), copy.Source);

            // The source is captured when WithFile runs; a later current-directory change must
            // not alter the file a subsequent Start would copy.
            Directory.SetCurrentDirectory(original);
            Assert.Equal(Path.GetFullPath(file), Assert.Single(container.Configuration.Files).Source);
        }
        finally
        {
            Directory.SetCurrentDirectory(original);
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void WithFile_relative_and_absolute_sources_hash_identically()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-absolutize-hash");
        var original = Directory.GetCurrentDirectory();
        try
        {
            var file = Path.Combine(directory.FullName, "payload.txt");
            File.WriteAllText(file, "payload");
            Directory.SetCurrentDirectory(directory.FullName);

            var relative = new WslContainerBuilder().WithImage("alpine").WithFile("payload.txt", "/tmp/payload.txt").Build();
            var absolute = new WslContainerBuilder().WithImage("alpine").WithFile(file, "/tmp/payload.txt").Build();

            Assert.Equal(
                WslConfigHasher.Compute(relative.Configuration),
                WslConfigHasher.Compute(absolute.Configuration));
        }
        finally
        {
            Directory.SetCurrentDirectory(original);
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void WithVolume_stores_an_absolute_host_path_at_build_time()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-absolutize-volume");
        var original = Directory.GetCurrentDirectory();
        try
        {
            var mount = Directory.CreateDirectory(Path.Combine(directory.FullName, "data")).FullName;
            Directory.SetCurrentDirectory(directory.FullName);

            var container = new WslContainerBuilder()
                .WithImage("alpine")
                .WithVolume("data", "/data")
                .Build();

            var volume = Assert.Single(container.Configuration.Volumes);
            Assert.True(Path.IsPathFullyQualified(volume.HostPath));
            Assert.Equal(Path.GetFullPath(mount), volume.HostPath);

            Directory.SetCurrentDirectory(original);
            Assert.Equal(Path.GetFullPath(mount), Assert.Single(container.Configuration.Volumes).HostPath);
        }
        finally
        {
            Directory.SetCurrentDirectory(original);
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void WithVolume_requires_an_existing_directory()
    {
        var builder = new WslContainerBuilder();

        Assert.Throws<WslcException>(() => builder.WithVolume("missing-directory", "/data"));
    }

    [Fact]
    public void Container_paths_reject_injection_across_builder_methods()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-builder-test");
        var file = Path.Combine(directory.FullName, "payload.txt");
        File.WriteAllText(file, "payload");
        try
        {
            var builder = new WslContainerBuilder().WithImage("alpine");
            Assert.Throws<ArgumentException>(() => builder.WithFile(file, "/proc/self/environ"));
            Assert.Throws<ArgumentException>(() => builder.WithVolume(directory.FullName, "/sys/kernel"));
            Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("data", "/dev/sda", 100));
            Assert.Throws<ArgumentException>(() => builder.WithSessionVolume("data", "/a/../b", 100));
            Assert.Throws<ArgumentException>(() => new WslContainerBuilder().WithWorkingDirectory("/tmp/../etc"));
            Assert.Throws<ArgumentException>(() => new WslContainerBuilder().WithWorkingDirectory("/tmp/bad\u0007name"));
            Assert.Throws<ArgumentException>(() => new WslContainerBuilder().WithWorkingDirectory("/./proc/self"));

            // Spaces are legal inside container paths even though the HTTP probe rejects them.
            builder.WithWorkingDirectory("/mnt/c/Program Files/data");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void WithFile_rejects_reparse_point_sources()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-reparse");
        try
        {
            var target = Path.Combine(directory.FullName, "target.txt");
            File.WriteAllText(target, "payload");
            var link = Path.Combine(directory.FullName, "link.txt");
            try
            {
                File.CreateSymbolicLink(link, target);
            }
            catch (Exception exception) when (exception is UnauthorizedAccessException or IOException or PlatformNotSupportedException)
            {
                Assert.Skip("Symbolic link creation is not available in this environment.");
            }

            Assert.Throws<WslcException>(() => new WslContainerBuilder().WithFile(link, "/tmp/link.txt"));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void WithVolume_rejects_reparse_point_sources()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-reparse");
        try
        {
            var target = Directory.CreateDirectory(Path.Combine(directory.FullName, "target")).FullName;
            var link = Path.Combine(directory.FullName, "link");
            try
            {
                Directory.CreateSymbolicLink(link, target);
            }
            catch (Exception exception) when (exception is UnauthorizedAccessException or IOException or PlatformNotSupportedException)
            {
                Assert.Skip("Symbolic link creation is not available in this environment.");
            }

            Assert.Throws<WslcException>(() => new WslContainerBuilder().WithVolume(link, "/data"));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
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

    [Fact]
    public void Cpu_and_memory_caps_are_inclusive()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithCpuCount(BuilderLimits.MaxCpuCount)
            .WithMemoryMB(BuilderLimits.MaxMemoryMB)
            .Build();

        Assert.Equal(BuilderLimits.MaxCpuCount, container.Configuration.CpuCount);
        Assert.Equal(BuilderLimits.MaxMemoryMB, container.Configuration.MemorySizeInMB);

        var builder = new WslContainerBuilder();
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithCpuCount(BuilderLimits.MaxCpuCount + 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithMemoryMB(BuilderLimits.MaxMemoryMB + 1));
    }

    [Fact]
    public void Session_volume_size_cap_is_inclusive()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithSessionVolume("data", "/data", BuilderLimits.MaxSessionVolumeBytes)
            .Build();

        Assert.Equal(
            BuilderLimits.MaxSessionVolumeBytes,
            Assert.Single(container.Configuration.SessionVolumes).SizeBytes);
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new WslContainerBuilder().WithSessionVolume("data", "/data", BuilderLimits.MaxSessionVolumeBytes + 1));
    }

    [Fact]
    public void Startup_timeout_cap_is_inclusive_and_rejects_longer_timeouts_before_start()
    {
        var container = new WslContainerBuilder()
            .WithImage("alpine")
            .WithStartupTimeout(BuilderLimits.MaxStartupTimeout)
            .Build();

        Assert.Equal(BuilderLimits.MaxStartupTimeout, container.Configuration.StartupTimeout);

        var builder = new WslContainerBuilder().WithImage("alpine");
        Assert.Throws<ArgumentOutOfRangeException>(
            () => builder.WithStartupTimeout(BuilderLimits.MaxStartupTimeout + TimeSpan.FromSeconds(1)));

        // 3650 days is past the ~49.7-day CancellationTokenSource ceiling; the builder must
        // reject it while configuring, never through the startup token source at Start.
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.WithStartupTimeout(TimeSpan.FromDays(3650)));
    }

    [Fact]
    public void Wait_strategy_count_cap_is_enforced_at_build()
    {
        var builder = new WslContainerBuilder()
            .WithImage("alpine")
            .WithStartupTimeout(BuilderLimits.MaxStartupTimeout);
        for (var i = 0; i < BuilderLimits.MaxWaitStrategies; i++)
        {
            builder.WithWaitStrategy(
                Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.FromSeconds(1)).UntilFileExists($"/tmp/ready-{i}"));
        }

        Assert.Equal(BuilderLimits.MaxWaitStrategies, builder.Build().Configuration.WaitStrategies.Count);

        builder.WithWaitStrategy(
            Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.FromSeconds(1)).UntilFileExists("/tmp/one-too-many"));
        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void Wait_timeout_sum_saturates_instead_of_overflowing()
    {
        var builder = new WslContainerBuilder()
            .WithImage("alpine")
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.MaxValue).UntilFileExists("/tmp/a"))
            .WithWaitStrategy(Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.MaxValue).UntilFileExists("/tmp/b"));

        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void Command_argument_count_cap_is_enforced_at_build()
    {
        var atCap = new string[BuilderLimits.MaxCommandArguments];
        Array.Fill(atCap, "arg");
        var container = new WslContainerBuilder().WithImage("alpine").WithCommand("echo", atCap).Build();

        Assert.Equal(BuilderLimits.MaxCommandArguments, container.Configuration.CommandArguments.Count);

        var overCap = new string[BuilderLimits.MaxCommandArguments + 1];
        Array.Fill(overCap, "arg");
        var builder = new WslContainerBuilder().WithImage("alpine").WithCommand("echo", overCap);
        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void Environment_value_and_count_caps_are_enforced()
    {
        var atCap = new string('a', BuilderLimits.MaxEnvironmentValueBytes);
        var container = new WslContainerBuilder().WithImage("alpine").WithEnvironment("BIG", atCap).Build();

        Assert.Equal(atCap, container.Configuration.Environment["BIG"]);
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new WslContainerBuilder().WithEnvironment("BIG", atCap + "a"));

        var builder = new WslContainerBuilder().WithImage("alpine");
        for (var i = 0; i < BuilderLimits.MaxEnvironmentVariables; i++)
        {
            builder.WithEnvironment($"VAR_{i}", "1");
        }

        Assert.Equal(BuilderLimits.MaxEnvironmentVariables, builder.Build().Configuration.Environment.Count);

        builder.WithEnvironment("VAR_ONE_TOO_MANY", "1");
        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void File_and_volume_count_caps_are_enforced_at_build()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-limits");
        try
        {
            var source = Path.Combine(directory.FullName, "payload.txt");
            File.WriteAllText(source, "payload");

            var files = new WslContainerBuilder().WithImage("alpine");
            for (var i = 0; i < BuilderLimits.MaxFileCopies; i++)
            {
                files.WithFile(source, $"/tmp/payload-{i}.txt");
            }

            Assert.Equal(BuilderLimits.MaxFileCopies, files.Build().Configuration.Files.Count);
            files.WithFile(source, "/tmp/payload-one-too-many.txt");
            Assert.Throws<WslcException>(() => files.Build());

            var volumes = new WslContainerBuilder().WithImage("alpine");
            for (var i = 0; i < BuilderLimits.MaxVolumeMounts; i++)
            {
                volumes.WithVolume(directory.FullName, $"/data-{i}");
            }

            Assert.Equal(BuilderLimits.MaxVolumeMounts, volumes.Build().Configuration.Volumes.Count);
            volumes.WithVolume(directory.FullName, "/data-one-too-many");
            Assert.Throws<WslcException>(() => volumes.Build());
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void Module_builder_rejects_an_unbounded_wait_timeout_at_build()
    {
        var builder = new ProbeModuleBuilder().WithWaitTimeout(TimeSpan.FromDays(3650));

        // The derived startup timeout is capped; the failure must happen while the module
        // builds, not from the startup CancellationTokenSource once the container starts.
        Assert.Throws<ArgumentOutOfRangeException>(() => builder.BuildForTest());
    }

    [Fact]
    public async Task Exec_timeout_cap_is_enforced()
    {
        await using var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Timeout = BuilderLimits.MaxExecTimeout + TimeSpan.FromSeconds(1) },
                CancellationToken.None));

        // At the cap validation passes; an unstarted container then fails with WslcException,
        // which proves the timeout itself was accepted.
        await Assert.ThrowsAsync<WslcException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Timeout = BuilderLimits.MaxExecTimeout },
                CancellationToken.None));
    }

    [Fact]
    public async Task Exec_rejects_more_than_the_argument_cap()
    {
        await using var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        var atCap = new string[BuilderLimits.MaxCommandArguments];
        Array.Fill(atCap, "arg");
        // At the cap the arguments pass validation; the unstarted container then fails, which
        // proves the list itself was accepted.
        var atCapException = await Assert.ThrowsAsync<WslcException>(() => container.ExecAsync("echo", atCap));
        Assert.DoesNotContain("Too many", atCapException.Message);

        var overCap = new string[BuilderLimits.MaxCommandArguments + 1];
        Array.Fill(overCap, "arg");
        var overCapException = await Assert.ThrowsAsync<WslcException>(() => container.ExecAsync("echo", overCap));
        Assert.Contains("maximum", overCapException.Message);
    }

    [Fact]
    public async Task Exec_rejects_environment_above_the_caps()
    {
        await using var container = new WslContainerBuilder().WithImage("alpine:latest").Build();

        var atCap = new string('a', BuilderLimits.MaxEnvironmentValueBytes);
        var atCapException = await Assert.ThrowsAsync<WslcException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Environment = new Dictionary<string, string> { ["BIG"] = atCap } },
                CancellationToken.None));
        Assert.DoesNotContain("Environment variable", atCapException.Message);

        var overValue = await Assert.ThrowsAsync<ArgumentOutOfRangeException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Environment = new Dictionary<string, string> { ["BIG"] = atCap + "a" } },
                CancellationToken.None));
        Assert.Contains("maximum", overValue.Message);

        var overCount = new Dictionary<string, string>();
        for (var i = 0; i <= BuilderLimits.MaxEnvironmentVariables; i++)
        {
            overCount[$"VAR_{i}"] = "1";
        }

        var overCountException = await Assert.ThrowsAsync<WslcException>(
            () => container.ExecAsync(
                "echo",
                new ExecOptions { Environment = overCount },
                CancellationToken.None));
        Assert.Contains("maximum", overCountException.Message);
    }

    [Fact]
    public void Composite_wait_count_cap_is_enforced_at_composition()
    {
        var strategy = Wslc.Testcontainers.Waiting.Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(1))
            .UntilFileExists("/tmp/wait-0");
        for (var i = 1; i < BuilderLimits.MaxWaitStrategies; i++)
        {
            strategy = strategy.And(
                Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.FromSeconds(1)).UntilFileExists($"/tmp/wait-{i}"));
        }

        var composite = Assert.IsType<Wslc.Testcontainers.Waiting.CompositeWaitStrategy>(strategy);
        Assert.Equal(BuilderLimits.MaxWaitStrategies, composite.Strategies.Count);
        Assert.Throws<WslcException>(
            () => strategy.And(
                Wslc.Testcontainers.Waiting.Wait.ForWsl().WithTimeout(TimeSpan.FromSeconds(1)).UntilFileExists("/tmp/one-too-many")));
    }

    [Fact]
    public void Session_volume_count_cap_is_enforced_at_build()
    {
        var builder = new WslContainerBuilder().WithImage("alpine");
        for (var i = 0; i < BuilderLimits.MaxSessionVolumes; i++)
        {
            builder.WithSessionVolume($"data{i}", $"/data{i}", 1024);
        }

        Assert.Equal(BuilderLimits.MaxSessionVolumes, builder.Build().Configuration.SessionVolumes.Count);

        builder.WithSessionVolume("one-too-many", "/data-extra", 1024);
        Assert.Throws<WslcException>(() => builder.Build());
    }

    [Fact]
    public void FromTarball_rejects_a_tarball_above_the_size_cap()
    {
        var path = Path.Combine(Path.GetTempPath(), $"wslc-{Guid.NewGuid():N}.tar");
        try
        {
            using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                try
                {
                    stream.SetLength((long)BuilderLimits.MaxTarballBytes);
                }
                catch (IOException exception)
                {
                    Assert.Skip($"Sparse file allocation is not available: {exception.Message}");
                }

                // At the cap the tarball is accepted.
                _ = new WslContainerBuilder().FromTarball(path);

                try
                {
                    stream.SetLength((long)BuilderLimits.MaxTarballBytes + 1);
                }
                catch (IOException exception)
                {
                    Assert.Skip($"Sparse file allocation is not available: {exception.Message}");
                }
            }

            Assert.Throws<WslcException>(() => new WslContainerBuilder().FromTarball(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    private sealed class ProbeModuleBuilder : WslModuleBuilder<ProbeModuleBuilder>
    {
        public ProbeModuleBuilder()
            : base("alpine", port: 80, readyMessage: "ready")
        {
        }

        public IWslContainer BuildForTest() => BuildContainer();
    }
}
