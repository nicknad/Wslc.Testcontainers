using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslInstanceStoreTests : IDisposable
{
    private readonly string _root;
    private readonly WslInstanceStore _store;

    public WslInstanceStoreTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "wslc-tests", Guid.NewGuid().ToString("N"));
        _store = new WslInstanceStore(_root, "test-session");
    }

    public void Dispose() => WslInstanceStore.BestEffortDeleteDirectory(_root);

    [Fact]
    public void Metadata_roundtrips()
    {
        var metadata = CreateMetadata();

        _store.WriteMetadata(metadata);
        var loaded = _store.TryReadMetadata(metadata.InstanceId);

        Assert.NotNull(loaded);
        Assert.Equal(metadata.SessionId, loaded.SessionId);
        Assert.Equal(metadata.InstanceId, loaded.InstanceId);
        Assert.Equal(metadata.OwnerProcessId, loaded.OwnerProcessId);
        Assert.Equal(metadata.Image, loaded.Image);
        Assert.Equal(metadata.Reuse, loaded.Reuse);
        Assert.Equal(metadata.State, loaded.State);
    }

    [Fact]
    public void Missing_metadata_returns_null()
    {
        Assert.Null(_store.TryReadMetadata("wslc-does-not-exist"));
    }

    [Fact]
    public void Deleting_an_instance_directory_removes_metadata_and_storage()
    {
        var metadata = CreateMetadata();
        _store.WriteMetadata(metadata);
        File.WriteAllText(Path.Combine(_store.GetInstanceDirectory(metadata.InstanceId), "ext4.vhdx"), "data");

        _store.DeleteInstanceDirectory(metadata.InstanceId);

        Assert.False(Directory.Exists(_store.GetInstanceDirectory(metadata.InstanceId)));
        Assert.Null(_store.TryReadMetadata(metadata.InstanceId));
    }

    [Fact]
    public void Deleting_a_junction_keeps_its_target()
    {
        var target = Path.Combine(_root, "junction-target");
        var junction = Path.Combine(_root, "junction-link");
        Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(target, "marker.txt"), "data");

        Assert.True(CreateDirectoryJunction(junction, target), "mklink /J failed");

        WslInstanceStore.BestEffortDeleteDirectory(junction);

        Assert.False(Directory.Exists(junction));
        Assert.True(File.Exists(Path.Combine(target, "marker.txt")));
    }

    [Fact]
    public void Instance_directory_is_sanitized()
    {
        var directory = _store.GetInstanceDirectory("wslc-a/b:c");

        Assert.Equal(Path.Combine(_store.InstancesDirectory, "wslc-a_b_c"), directory);
    }

    [Theory]
    [InlineData("...")]
    [InlineData("a.")]
    [InlineData("a. ")]
    public void Trailing_dots_and_spaces_do_not_alias_other_instances(string instanceName)
    {
        var instancesDirectory = Path.GetFullPath(_store.InstancesDirectory);
        var normal = Path.GetFullPath(_store.GetInstanceDirectory("a"));
        var directory = _store.GetInstanceDirectory(instanceName);

        Assert.NotEqual(instancesDirectory, directory);
        Assert.NotEqual(normal, directory);
    }

    [Theory]
    [InlineData("...", "___")]
    [InlineData("a.", "a_")]
    [InlineData("a. ", "a._")]
    public void Trailing_dots_and_spaces_are_replaced(string value, string expected)
    {
        Assert.Equal(expected, WslInstanceStore.Sanitize(value));
    }

    [Fact]
    public void Normal_instance_names_are_unchanged_by_sanitization()
    {
        Assert.Equal("wslc-a_b.c", WslInstanceStore.Sanitize("wslc-a_b.c"));
        Assert.Equal("wslc-test-1234abcd", WslInstanceStore.Sanitize("wslc-test-1234abcd"));
    }

    [Theory]
    [InlineData("..")]
    [InlineData("../..")]
    [InlineData("..\\..\\escape")]
    [InlineData("wslc-a/b:c")]
    [InlineData("...")]
    public void Instance_directory_cannot_escape_the_instances_directory(string instanceName)
    {
        string? directory = null;
        var exception = Record.Exception(() => directory = _store.GetInstanceDirectory(instanceName));

        if (exception is not null)
        {
            Assert.IsType<WslcException>(exception);
            return;
        }

        var prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(_store.InstancesDirectory))
            + Path.DirectorySeparatorChar;
        Assert.True(
            Path.GetFullPath(directory!).StartsWith(prefix, StringComparison.OrdinalIgnoreCase),
            $"'{instanceName}' escaped to '{directory}'.");
    }

    [Fact]
    public void Concurrent_metadata_writes_do_not_race()
    {
        var metadata = CreateMetadata();

        Parallel.For(0, 100, index =>
        {
            _store.WriteMetadata(metadata with { State = index % 2 == 0 ? "Running" : "Stopped" });
            Assert.NotNull(_store.TryReadMetadata(metadata.InstanceId));
        });

        var instanceDirectory = _store.GetInstanceDirectory(metadata.InstanceId);
        Assert.Empty(Directory.EnumerateFiles(instanceDirectory, "*.tmp"));
    }

    private static bool CreateDirectoryJunction(string link, string target)
    {
        var startInfo = new System.Diagnostics.ProcessStartInfo("cmd.exe", $"/c mklink /J \"{link}\" \"{target}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        using var process = System.Diagnostics.Process.Start(startInfo);
        Assert.NotNull(process);
        process.WaitForExit();
        return process.ExitCode == 0;
    }

    private WslInstanceMetadata CreateMetadata() => new(
        _store.SessionId,
        "wslc-test-1234abcd",
        Environment.ProcessId,
        DateTimeOffset.UtcNow)
    {
        State = "Running",
        Owner = "tester",
        Image = "alpine:latest",
        Reuse = false,
    };
}
