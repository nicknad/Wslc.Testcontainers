using System.Globalization;

namespace Wslc.Testcontainers;

/// <summary>
/// Process-wide WSLC configuration resolved from environment variables. Code-level builder
/// configuration always takes precedence over these defaults.
/// Truthy values are <c>1/true/yes/on</c>; falsy are <c>0/false/no/off</c> (case-insensitive).
/// Any other non-empty value is treated as unset (falls back to defaults) — check spelling if
/// a flag seems ignored. Snapshots (<see cref="DataDirectory"/>, <see cref="SessionId"/>) are
/// captured on first use; changing env vars afterwards has no effect.
/// </summary>
public static class WslcEnvironment
{
    /// <summary>Default readiness timeout, in seconds or as a <see cref="TimeSpan"/> string.</summary>
    public const string TimeoutVariable = "WSLC_TIMEOUT";

    /// <summary>Directory used to store WSLC instances and caches. Snapshotted on first store access.</summary>
    public const string DataDirectoryVariable = "WSLC_DATA_DIRECTORY";

    /// <summary>Container image used when a builder does not specify one.</summary>
    public const string DefaultImageVariable = "WSLC_DEFAULT_IMAGE";

    /// <summary>Enables reuse by default when set to a truthy value (<c>1/true/yes/on</c>).</summary>
    public const string ReuseVariable = "WSLC_REUSE";

    /// <summary>Set to a falsy value (<c>0/false/no/off</c>) to disable automatic cleanup and the orphan reaper.</summary>
    public const string CleanupVariable = "WSLC_CLEANUP";

    /// <summary>Set to a truthy value to allow reuse while running under CI (otherwise reuse is forced off).</summary>
    public const string ReuseInCiVariable = "WSLC_REUSE_IN_CI";

    /// <summary>Overrides the session identifier used to name WSLC instances. Snapshotted once per process.</summary>
    public const string SessionIdVariable = "WSLC_SESSION_ID";

    internal static readonly TimeSpan DefaultWaitTimeoutValue = TimeSpan.FromSeconds(60);

    /// <summary>Gets the configured default image, when any.</summary>
    public static string? DefaultImage => GetNonEmpty(DefaultImageVariable);

    /// <summary>Gets the data directory used for instance metadata and caches. Snapshotted on first use.</summary>
    public static string DataDirectory { get; } =
        GetNonEmpty(DataDirectoryVariable)
        ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Wslc");

    /// <summary>Gets a value indicating whether reuse is enabled by default.</summary>
    public static bool ReuseByDefault => ParseBool(GetNonEmpty(ReuseVariable)) ?? false;

    /// <summary>Gets a value indicating whether automatic cleanup (including the orphan reaper) is enabled.</summary>
    public static bool CleanupEnabled => ParseBool(GetNonEmpty(CleanupVariable)) ?? true;

    /// <summary>Gets the session identifier, generating a process-unique value when unset.</summary>
    public static string SessionId { get; } = GetNonEmpty(SessionIdVariable) ?? CreateSessionId();

    internal static TimeSpan DefaultWaitTimeout => ParseTimeout(GetNonEmpty(TimeoutVariable)) ?? DefaultWaitTimeoutValue;

    internal static bool ReuseAllowed =>
        !IsContinuousIntegration || (ParseBool(GetNonEmpty(ReuseInCiVariable)) ?? false);

    private static readonly string[] ContinuousIntegrationVariables =
        { "CI", "TF_BUILD", "GITHUB_ACTIONS", "JENKINS_URL", "TEAMCITY_VERSION" };

    private static string CreateSessionId()
    {
        var entry = System.Reflection.Assembly.GetEntryAssembly()?.GetName().Name ?? "wslc";
        return $"{entry}-{Environment.ProcessId}";
    }

    internal static bool IsContinuousIntegrationVariable(string name, string? value) =>
        name switch
        {
            "CI" or "TF_BUILD" or "GITHUB_ACTIONS" => ParseBool(value) ?? false,
            "JENKINS_URL" or "TEAMCITY_VERSION" => !string.IsNullOrWhiteSpace(value),
            _ => false,
        };

    private static bool IsContinuousIntegration
    {
        get
        {
            foreach (var name in ContinuousIntegrationVariables)
            {
                if (IsContinuousIntegrationVariable(name, GetNonEmpty(name)))
                {
                    return true;
                }
            }

            return false;
        }
    }

    private static string? GetNonEmpty(string name)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? null : value.Trim();
    }

    internal static bool? ParseBool(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        return value.Trim().ToLowerInvariant() switch
        {
            "1" or "true" or "yes" or "on" => true,
            "0" or "false" or "no" or "off" => false,
            _ => null,
        };
    }

    internal static TimeSpan? ParseTimeout(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        if (double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var seconds) && seconds > 0)
        {
            return TimeSpan.FromSeconds(seconds);
        }

        return TimeSpan.TryParse(value, CultureInfo.InvariantCulture, out var parsed) && parsed > TimeSpan.Zero
            ? parsed
            : null;
    }
}
