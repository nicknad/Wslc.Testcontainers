using System.Diagnostics;

namespace Wslc.Testcontainers.Provisioning;

/// <summary>
/// Removes storage left behind by WSLC sessions whose owning process no longer exists.
/// The official WSL container API does not expose session enumeration, so cleanup is
/// limited to WSLC-owned storage directories.
/// </summary>
public static class WslResourceReaper
{
    /// <summary>Deletes storage of abandoned ephemeral instances. Reusable instances are preserved.</summary>
    public static Task<IReadOnlyList<string>> CleanupAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CleanupCore(includeReusable: false, cancellationToken), cancellationToken);

    /// <summary>Deletes storage of abandoned instances including reusable ones.</summary>
    public static Task<IReadOnlyList<string>> CleanupAllAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CleanupCore(includeReusable: true, cancellationToken), cancellationToken);

    internal static IReadOnlyList<string> CleanupCore(bool includeReusable, CancellationToken cancellationToken)
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
            var ownerAlive = metadata is not null && IsOwnerAlive(metadata.OwnerProcessId);
            if (!ShouldCleanup(metadata, ownerAlive, includeReusable))
            {
                continue;
            }

            WslInstanceStore.BestEffortDeleteDirectory(directory);
            removed.Add(name);
        }

        return removed;
    }

    /// <summary>Decides whether WSLC-owned storage may be deleted automatically.</summary>
    internal static bool ShouldCleanup(WslInstanceMetadata? metadata, bool ownerAlive, bool includeReusable = false)
    {
        if (metadata is null)
        {
            // No metadata: not created by WSLC. Never delete.
            return false;
        }

        if (metadata.Reuse && !includeReusable)
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
    }
}
