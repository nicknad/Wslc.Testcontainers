using System.Globalization;
using System.Net.Http;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Provisioning;
using Wslc.Testcontainers.Tests.Support;
using Wslc.Testcontainers.Waiting;
using Xunit;

namespace Wslc.Testcontainers.Tests;

/// <summary>
/// End-to-end tests against the real WSL container runtime. Enable with
/// <c>WSLC_RUN_INTEGRATION=1</c>. They pull public images and require WSL 2.9.3+.
/// </summary>
public sealed class IntegrationTests
{
    private const string TestImage = "docker.io/library/alpine:latest";

    // Ships iptables/ip6tables, python3 and curl, so egress rules can be installed and probed.
    private const string IptablesImage = "docker.io/nicolaka/netshoot:latest";

    [IntegrationFact]
    public void Runtime_components_are_available()
    {
        Assert.Empty(WslcService.GetMissingComponents());
        Assert.True(WslcService.GetVersion().Major >= 2);
    }

    [IntegrationFact]
    public async Task Runs_commands_and_captures_output()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .Build();

        await container.StartAsync();

        var result = await container.ExecAsync("/bin/sh", "-c", "echo hello-wslc");

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("hello-wslc", result.Stdout);
        Assert.True(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Exposes_environment_variables()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithEnvironment("WSLC_TEST_VALUE", "hello")
            .Build();

        await container.StartAsync();

        var result = await container.ExecAsync("/bin/sh", "-c", "printf %s \"$WSLC_TEST_VALUE\"");

        Assert.Equal(0, result.ExitCode);
        Assert.Equal("hello", result.Stdout);
    }

    [IntegrationFact]
    public async Task Copies_files_into_and_out_of_the_container()
    {
        var source = Path.Combine(Path.GetTempPath(), $"wslc-{Guid.NewGuid():N}.txt");
        var destination = source + ".out";
        await File.WriteAllTextAsync(source, "wslc-file-content");

        try
        {
            await using var container = new WslContainerBuilder()
                .FromImage(TestImage)
                .Build();

            await container.StartAsync();

            await container.CopyToAsync(source, "/tmp/wslc-test.txt");
            var cat = await container.ExecAsync("/bin/cat", "/tmp/wslc-test.txt");
            Assert.Equal("wslc-file-content", cat.Stdout);

            await container.CopyFromAsync("/tmp/wslc-test.txt", destination);
            Assert.Equal("wslc-file-content", await File.ReadAllTextAsync(destination));
        }
        finally
        {
            File.Delete(source);
            if (File.Exists(destination))
            {
                File.Delete(destination);
            }
        }
    }

    [IntegrationFact]
    public async Task Maps_ports_and_serves_http()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithCommand(
                "/bin/sh",
                "-c",
                "while true; do printf 'HTTP/1.1 200 OK\\r\\nContent-Length: 2\\r\\nConnection: close\\r\\n\\r\\nok' | nc -l -p 8080; done")
            .WithPort(8080)
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(TimeSpan.FromSeconds(60))
                    .UntilHttpRequestIsSucceeded("/", 8080))
            .Build();

        await container.StartAsync();

        using var client = new HttpClient();
        var response = await client.GetAsync($"http://{container.Host}:{container.GetMappedPort(8080)}/");

        Assert.True(response.IsSuccessStatusCode);
    }

    [IntegrationFact]
    public async Task Readiness_failure_cleans_up_ephemeral_storage()
    {
        var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithPort(65000)
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(TimeSpan.FromSeconds(5))
                    .WithRetryInterval(TimeSpan.FromMilliseconds(200))
                    .UntilMessageIsLogged("this-message-never-appears"))
            .Build();

        var name = container.Name;

        await Assert.ThrowsAsync<WslReadinessException>(() => container.StartAsync());
        Assert.False(Directory.Exists(WslInstanceStore.Default.GetInstanceDirectory(name)));

        await container.DisposeAsync();
    }

    [IntegrationFact]
    public async Task Isolated_networking_runs_commands_without_ports()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithNetworkingMode(ContainerNetworkMode.None)
            .Build();

        await container.StartAsync();

        var result = await container.ExecAsync("/bin/sh", "-c", "echo offline-ok");

        Assert.Equal(0, result.ExitCode);
        Assert.Contains("offline-ok", result.Stdout);
        Assert.Throws<WslNetworkException>(() => container.GetMappedPort(8080));
    }

    [IntegrationFact]
    public async Task Egress_allowlist_startup_failure_reports_guidance_and_cleans_up()
    {
        // docker.io/library/alpine has no iptables: startup must fail with guidance,
        // not silently leave egress open, and must not leak the ephemeral instance.
        var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithEgressAllowlist(new EgressAllowlistOptions { AllowedHosts = new[] { "10.0.0.5" } })
            .Build();
        var name = container.Name;

        var exception = await Assert.ThrowsAsync<WslProvisioningException>(() => container.StartAsync());

        Assert.Contains("iptables", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(container.IsStarted);
        Assert.False(Directory.Exists(WslInstanceStore.Default.GetInstanceDirectory(name)));

        await container.DisposeAsync();
    }

    [IntegrationFact]
    public async Task Egress_allowlist_fails_closed_when_iptables_cannot_manage_rules()
    {
        // The WSLC runtime does not grant CAP_NET_ADMIN, so even an image with iptables
        // (netshoot) cannot install the policy. Startup must fail with guidance and must
        // not leave a partially applied or silently open policy behind.
        var container = new WslContainerBuilder()
            .FromImage(IptablesImage)
            .WithEgressAllowlist(new EgressAllowlistOptions { AllowedHosts = new[] { "10.0.0.5" } })
            .Build();
        var name = container.Name;

        var exception = await Assert.ThrowsAsync<WslProvisioningException>(() => container.StartAsync());

        Assert.Contains("CAP_NET_ADMIN", exception.Message);
        Assert.False(container.IsStarted);
        Assert.False(Directory.Exists(WslInstanceStore.Default.GetInstanceDirectory(name)));

        await container.DisposeAsync();
    }

    [IntegrationFact]
    public async Task Session_volumes_mount_and_are_recreated_on_restart()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithSessionVolume("scratch", "/scratch", 64UL * 1024 * 1024)
            .Build();

        await container.StartAsync();

        var write = await container.ExecAsync("/bin/sh", "-c", "echo persisted > /scratch/data.txt && cat /scratch/data.txt");
        Assert.Equal(0, write.ExitCode);
        Assert.Equal("persisted", write.Stdout.Trim());

        await container.StopAsync();
        await container.StartAsync();

        var exists = await container.ExecAsync("/bin/sh", "-c", "test -e /scratch/data.txt");
        Assert.NotEqual(0, exists.ExitCode);
    }

    [IntegrationFact]
    public async Task Read_only_session_volumes_reject_writes()
    {
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithSessionVolume("scratch", "/scratch", 64UL * 1024 * 1024, VolumeAccess.ReadOnly, VhdAllocationType.Fixed)
            .Build();

        await container.StartAsync();

        var mount = await container.ExecAsync("/bin/sh", "-c", "test -d /scratch");
        Assert.Equal(0, mount.ExitCode);

        var write = await container.ExecAsync("/bin/sh", "-c", "echo nope > /scratch/data.txt");
        Assert.NotEqual(0, write.ExitCode);
    }

    [IntegrationFact]
    public async Task Resource_caps_are_applied_to_the_session()
    {
        var cpuCount = (uint)Math.Min(2, Environment.ProcessorCount);
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .WithCpuCount(cpuCount)
            .WithMemoryMB(1024)
            .Build();

        await container.StartAsync();

        var cpus = await container.ExecAsync("nproc");
        Assert.Equal(cpuCount, uint.Parse(cpus.Stdout.Trim(), CultureInfo.InvariantCulture));

        var memory = await container.ExecAsync("/bin/sh", "-c", "awk '/MemTotal/ {print $2}' /proc/meminfo");
        var memoryKb = long.Parse(memory.Stdout.Trim(), CultureInfo.InvariantCulture);
        Assert.InRange(memoryKb, 300_000, 1_500_000);
    }

    [IntegrationFact]
    public async Task Mounts_windows_directories_as_volumes()
    {
        var hostDirectory = Path.Combine(Path.GetTempPath(), $"wslc-volume-{Guid.NewGuid():N}");
        Directory.CreateDirectory(hostDirectory);
        await File.WriteAllTextAsync(Path.Combine(hostDirectory, "data.txt"), "mounted");

        try
        {
            await using var container = new WslContainerBuilder()
                .FromImage(TestImage)
                .WithVolume(hostDirectory, "/workspace")
                .Build();

            await container.StartAsync();

            var result = await container.ExecAsync("/bin/cat", "/workspace/data.txt");

            Assert.Equal(0, result.ExitCode);
            Assert.Equal("mounted", result.Stdout);
        }
        finally
        {
            Directory.Delete(hostDirectory, recursive: true);
        }
    }
}
