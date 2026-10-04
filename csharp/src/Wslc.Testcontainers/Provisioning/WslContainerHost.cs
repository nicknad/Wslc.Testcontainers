using System.Collections.Concurrent;
using Wslc.Testcontainers.Runtime;

namespace Wslc.Testcontainers.Provisioning;

/// <summary>
/// Process-wide WSLC initialization: verifies the WSL container runtime with the official
/// API, runs storage cleanup once, and guarantees best-effort cleanup of live containers
/// when the host process exits unexpectedly.
/// </summary>
internal static class WslContainerHost
{
    private static readonly Lazy<Task> Initialization = new(InitializeAsync, LazyThreadSafetyMode.ExecutionAndPublication);
    private static readonly ConcurrentDictionary<string, WslContainer> Live = new(StringComparer.OrdinalIgnoreCase);
    private static int _hooked;

    public static Task EnsureInitializedAsync(CancellationToken cancellationToken) =>
        Initialization.Value.WaitAsync(cancellationToken);

    public static void Register(WslContainer container) => Live[container.Name] = container;

    public static void Unregister(string name) => Live.TryRemove(name, out _);

    private static async Task InitializeAsync()
    {
        HookProcessExit();
        await Task.Run(WslcHost.EnsureAvailable).ConfigureAwait(false);

        if (WslcEnvironment.CleanupEnabled)
        {
            try
            {
                await WslResourceReaper.CleanupAsync(CancellationToken.None).ConfigureAwait(false);
            }
            catch
            {
                // Reaping is best effort and must never block container startup.
            }
        }
    }

    private static void HookProcessExit()
    {
        if (Interlocked.Exchange(ref _hooked, 1) != 0)
        {
            return;
        }

        AppDomain.CurrentDomain.ProcessExit += (_, _) => CleanupAll();
        AppDomain.CurrentDomain.UnhandledException += (_, _) => CleanupAll();
    }

    private static void CleanupAll()
    {
        foreach (var container in Live.Values)
        {
            try
            {
                container.CleanupSynchronously();
            }
            catch
            {
            }
        }
    }
}
