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
    public void Instance_directory_is_sanitized()
    {
        var directory = _store.GetInstanceDirectory("wslc-a/b:c");

        Assert.Equal(Path.Combine(_store.InstancesDirectory, "wslc-a_b_c"), directory);
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
