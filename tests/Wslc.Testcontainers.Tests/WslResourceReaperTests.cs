using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslResourceReaperTests
{
    [Fact]
    public void Unknown_metadata_is_never_cleaned()
    {
        Assert.False(WslResourceReaper.ShouldCleanup(null, ownerAlive: false));
        Assert.False(WslResourceReaper.ShouldCleanup(null, ownerAlive: true));
    }

    [Fact]
    public void Ephemeral_instances_are_cleaned_when_the_owner_is_gone()
    {
        var metadata = CreateMetadata(reuse: false);

        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: true));
        Assert.True(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: false));
    }

    [Fact]
    public void Reusable_instances_are_preserved()
    {
        var metadata = CreateMetadata(reuse: true);

        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: true));
        Assert.False(WslResourceReaper.ShouldCleanup(metadata, ownerAlive: false));
    }

    [Fact]
    public void Owner_liveness_is_detected()
    {
        Assert.True(WslResourceReaper.IsOwnerAlive(Environment.ProcessId));
        Assert.False(WslResourceReaper.IsOwnerAlive(0));
        Assert.False(WslResourceReaper.IsOwnerAlive(int.MaxValue));
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
