using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests.Provisioning;

public sealed class WslResourceReaperTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "wslc-tests", Guid.NewGuid().ToString("N"));
    private readonly WslInstanceStore _store;

    public WslResourceReaperTests() => _store = new WslInstanceStore(_root, "test-session");

    public void Dispose() => WslInstanceStore.BestEffortDeleteDirectory(_root);

    [Fact]
    public void Unknown_metadata_is_never_cleaned()
    {
        Assert.False(WslResourceReaper.ShouldCleanup(null, ownerAlive: false, includeReuse: false));
        Assert.False(WslResourceReaper.ShouldCleanup(null, ownerAlive: true, includeReuse: false));
    }

    [Fact]
    public void Ephemeral_instances_are_cleaned_when_the_owner_is_gone()
    {
        var metadata = CreateMetadata(reuse: false);

        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: true, includeReuse: false));
        Assert.True(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: false, includeReuse: false));
    }

    [Fact]
    public void Reusable_instances_are_preserved()
    {
        var metadata = CreateMetadata(reuse: true);

        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: true, includeReuse: false));
        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: false, includeReuse: false));
    }

    [Fact]
    public void Owner_liveness_is_detected()
    {
        // CreatedAt is "now" so the running PID cannot look recycled (StartTime predates it).
        Assert.True(WslResourceReaper.IsOwnerAlive(CreateMetadata(reuse: false)));
        Assert.False(WslResourceReaper.IsOwnerAlive(new WslInstanceMetadata("session", "wslc-test-0000", 0, DateTimeOffset.UtcNow)));
        Assert.False(WslResourceReaper.IsOwnerAlive(new WslInstanceMetadata("session", "wslc-test-0000", int.MaxValue, DateTimeOffset.UtcNow)));
    }

    [Fact]
    public void Owner_liveness_without_created_at_does_not_assume_pid_reuse()
    {
        // Before the fix the missing timestamp deserialized to year 1, so the running owner
        // process always looked like a recycled PID.
        var metadata = new WslInstanceMetadata("session", "wslc-test-0000", Environment.ProcessId, null);

        Assert.True(WslResourceReaper.IsOwnerAlive(metadata));
    }

    [Fact]
    public void Cleanup_grace_gates_metadata_without_created_at()
    {
        var name = "wslc-created-at-missing";
        var directory = _store.GetInstanceDirectory(name);
        Directory.CreateDirectory(directory);
        File.WriteAllText(
            Path.Combine(directory, "wslc.json"),
            """
            {
              "sessionId": "test-session",
              "instanceId": "wslc-created-at-missing",
              "ownerProcessId": 2147483647,
              "state": "Running",
              "reuse": false
            }
            """);

        Assert.Empty(WslResourceReaper.CleanupCore(_store, CancellationToken.None));
        Assert.True(Directory.Exists(directory));

        Directory.SetCreationTimeUtc(directory, DateTime.UtcNow - TimeSpan.FromDays(8));

        Assert.Equal(new[] { name }, WslResourceReaper.CleanupCore(_store, CancellationToken.None));
        Assert.False(Directory.Exists(directory));
    }

    [Fact]
    public void Cleanup_skips_metadata_that_names_another_instance()
    {
        var name = "wslc-identity-mismatch";
        var directory = _store.GetInstanceDirectory(name);
        Directory.CreateDirectory(directory);
        File.WriteAllText(
            Path.Combine(directory, "wslc.json"),
            """
            {
              "sessionId": "test-session",
              "instanceId": "wslc-some-other-instance",
              "ownerProcessId": 2147483647,
              "createdAt": "2020-01-01T00:00:00.000Z",
              "state": "Running",
              "reuse": false
            }
            """);
        Directory.SetCreationTimeUtc(directory, DateTime.UtcNow - TimeSpan.FromDays(30));

        Assert.Empty(WslResourceReaper.CleanupCore(_store, CancellationToken.None));
        Assert.True(Directory.Exists(directory));
    }

    [Fact]
    public void Purge_reuse_skips_metadata_that_names_another_instance()
    {
        var name = "wslc-reuse-identity";
        var directory = _store.GetInstanceDirectory(name);
        Directory.CreateDirectory(directory);
        File.WriteAllText(
            Path.Combine(directory, "wslc.json"),
            """
            {
              "sessionId": "test-session",
              "instanceId": "wslc-some-other-instance",
              "ownerProcessId": 2147483647,
              "createdAt": "2020-01-01T00:00:00.000Z",
              "state": "Running",
              "reuse": true
            }
            """);
        Directory.SetCreationTimeUtc(directory, DateTime.UtcNow - TimeSpan.FromDays(30));

        Assert.Empty(WslResourceReaper.PurgeReuseCore(_store, CancellationToken.None));
        Assert.True(Directory.Exists(directory));
    }

    [Fact]
    public void Reuse_instance_lock_is_detected()
    {
        var directory = Path.Combine(Path.GetTempPath(), "wslc-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            Assert.False(WslResourceReaper.IsReuseInstanceInUse(directory));

            using (new FileStream(
                Path.Combine(directory, "wslc.lock"),
                FileMode.OpenOrCreate,
                FileAccess.ReadWrite,
                FileShare.None))
            {
                Assert.True(WslResourceReaper.IsReuseInstanceInUse(directory));
            }

            Assert.False(WslResourceReaper.IsReuseInstanceInUse(directory));
        }
        finally
        {
            WslInstanceStore.BestEffortDeleteDirectory(directory);
        }
    }

    private static WslInstanceMetadata CreateMetadata(bool reuse) => new(
        "session",
        "wslc-test-0000",
        Environment.ProcessId,
        DateTimeOffset.UtcNow)
    {
        Reuse = reuse,
    };
}
