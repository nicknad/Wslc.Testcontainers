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
    public async Task Egress_allowlist_requires_iptables_in_the_image()
    {
        // docker.io/library/alpine has no iptables: applying must fail with guidance,
        // not silently leave egress open.
        await using var container = new WslContainerBuilder()
            .FromImage(TestImage)
            .Build();

        await container.StartAsync();

        var options = new EgressAllowlistOptions { AllowedHosts = new[] { "10.0.0.5" } };
        await Assert.ThrowsAsync<WslProvisioningException>(() => container.ApplyEgressAllowlistAsync(options));
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
