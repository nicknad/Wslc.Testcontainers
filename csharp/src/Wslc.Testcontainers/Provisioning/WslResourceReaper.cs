using System.Diagnostics;

namespace Wslc.Testcontainers.Provisioning;

/// <summary>
/// Removes storage left behind by WSLC sessions whose owning process no longer exists.
/// The official WSL container API does not expose session enumeration, so cleanup is
/// limited to WSLC-owned storage directories (<c>wslc-*</c>).
/// </summary>
/// <remarks>
/// Ephemeral instances are deleted when the owner is gone. Reusable instances are preserved
/// by default (they survive owner exit by design) — call <see cref="PurgeReuseAsync"/> or
/// <see cref="CleanupIncludingReuseAsync"/> to reclaim
/// them. Directories with missing/corrupt metadata are deleted only after a 7-day grace period
/// to avoid removing just-crashed writes.
/// </remarks>
public static class WslResourceReaper
{
    private static readonly TimeSpan OrphanGracePeriod = TimeSpan.FromDays(7);

    /// <summary>Deletes storage of abandoned ephemeral instances. Reusable instances are preserved.</summary>
    /// <param name="cancellationToken">Cancellation token.</param>
    public static Task<IReadOnlyList<string>> CleanupAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CleanupCore(cancellationToken, includeReuse: false), cancellationToken);

    /// <summary>
    /// Deletes storage of abandoned instances including reusable ones whose owner is gone.
    /// Reuse instances currently held by a running process (<c>wslc.lock</c> acquired) are
    /// skipped, because their metadata owner may be a dead earlier process while another
    /// process is actively using the instance.
    /// Prefer <see cref="PurgeReuseAsync"/> to delete all reuse caches regardless of liveness.
    /// </summary>
    /// <param name="cancellationToken">Cancellation token.</param>
    public static Task<IReadOnlyList<string>> CleanupIncludingReuseAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CleanupCore(cancellationToken, includeReuse: true), cancellationToken);

    /// <summary>
    /// Force-deletes all reusable instances (<c>wslc-reuse-*</c>) regardless of owner
    /// liveness and without checking the instance lock; do not call while another process
    /// may be using one. Ephemeral orphans are left to <see cref="CleanupAsync(CancellationToken)"/>.
    /// </summary>
    public static Task<IReadOnlyList<string>> PurgeReuseAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => PurgeReuseCore(cancellationToken), cancellationToken);

    internal static IReadOnlyList<string> CleanupCore(CancellationToken cancellationToken, bool includeReuse = false)
    {
        var store = WslInstanceStore.Default;
        if (!Directory.Exists(store.InstancesDirectory))
        {
            return Array.Empty<string>();
        }

        var removed = new List<string>(8);

        foreach (var directory in Directory.EnumerateDirectories(store.InstancesDirectory))
        {
            cancellationToken.ThrowIfCancellationRequested();

            var name = Path.GetFileName(directory);
            if (!WslNaming.IsManaged(name))
            {
                continue;
            }

            var metadata = store.TryReadMetadata(name);
            if (metadata is null)
            {
                // Corrupt/missing metadata: only delete after grace period to avoid racing a
                // concurrent writer (write-then-rename should be atomic, but be conservative).
                if (GetDirectoryAge(directory) > OrphanGracePeriod)
                {
                    WslInstanceStore.BestEffortDeleteDirectory(directory);
                    removed.Add(name);
                }

                continue;
            }

            var ownerAlive = IsOwnerAlive(metadata);
            if (!ShouldCleanup(metadata, ownerAlive, includeReuse))
            {
                continue;
            }

            // A reuse instance's metadata owner is the process that first created it, which is
            // normally dead by design. The lock is the only reliable "currently in use" signal,
            // so never delete one another live process holds.
            if (metadata.Reuse && IsReuseInstanceInUse(directory))
            {
                continue;
            }

            WslInstanceStore.BestEffortDeleteDirectory(directory);
            removed.Add(name);
        }

        return removed;
    }

    internal static IReadOnlyList<string> PurgeReuseCore(CancellationToken cancellationToken)
    {
        var store = WslInstanceStore.Default;
        if (!Directory.Exists(store.InstancesDirectory))
        {
            return Array.Empty<string>();
        }

        var removed = new List<string>(8);
        foreach (var directory in Directory.EnumerateDirectories(store.InstancesDirectory))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var name = Path.GetFileName(directory);
            if (!WslNaming.IsManaged(name))
            {
                continue;
            }

            var metadata = store.TryReadMetadata(name);
            if (metadata?.Reuse != true)
            {
                continue;
            }

            WslInstanceStore.BestEffortDeleteDirectory(directory);
            removed.Add(name);
        }

        return removed;
    }

    /// <summary>Decides whether WSLC-owned storage may be deleted automatically.</summary>
    internal static bool ShouldCleanup(WslInstanceMetadata? metadata, bool ownerAlive) =>
        ShouldCleanup(metadata, ownerAlive, includeReuse: false);

    /// <summary>Decides whether WSLC-owned storage may be deleted, optionally including reuse.</summary>
    internal static bool ShouldCleanup(WslInstanceMetadata? metadata, bool ownerAlive, bool includeReuse)
    {
        if (metadata is null)
        {
            // No metadata: not created by WSLC. Never delete via this path (age-gated separately).
            return false;
        }

        if (metadata.Reuse && !includeReuse)
        {
            return false;
        }

        return !ownerAlive;
    }

    internal static bool IsOwnerAlive(int processId)
    {
        if (processId <= 0)
        {
            return false;
        }

        try
        {
            using var process = Process.GetProcessById(processId);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            // Access denied querying another user's process: assume alive to avoid deleting live storage.
            return true;
        }
    }

    /// <summary>Returns true when another process currently holds the instance reuse lock.</summary>
    internal static bool IsReuseInstanceInUse(string instanceDirectory)
    {
        var lockPath = Path.Combine(instanceDirectory, "wslc.lock");
        if (!File.Exists(lockPath))
        {
            return false;
        }

        try
        {
            using var stream = new FileStream(lockPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            return false;
        }
        catch (IOException)
        {
            return true;
        }
        catch (UnauthorizedAccessException)
        {
            // Cannot probe the lock (ACL); treat as in use rather than delete blind.
            return true;
        }
    }

    internal static bool IsOwnerAlive(WslInstanceMetadata metadata)
    {
        if (metadata.OwnerProcessId <= 0)
        {
            return false;
        }

        try
        {
            using var process = Process.GetProcessById(metadata.OwnerProcessId);
            if (process.HasExited)
            {
                return false;
            }

            // Guard PID recycling: if the current process with this PID started after the
            // instance was created, the original owner is gone and the PID was reused.
            try
            {
                if (process.StartTime.ToUniversalTime() > metadata.CreatedAt.UtcDateTime + TimeSpan.FromMinutes(1))
                {
                    return false;
                }
            }
            catch
            {
                // StartTime may throw for elevated/system processes; fall back to alive.
            }

            return true;
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            // Access denied querying another user's process: assume alive to avoid deleting live storage.
            return true;
        }
    }

    private static TimeSpan GetDirectoryAge(string directory)
    {
        try
        {
            var creation = Directory.GetCreationTimeUtc(directory);
            var age = DateTime.UtcNow - creation;
            return age < TimeSpan.Zero ? TimeSpan.Zero : age;
        }
        catch
        {
            return TimeSpan.Zero;
        }
    }
}
