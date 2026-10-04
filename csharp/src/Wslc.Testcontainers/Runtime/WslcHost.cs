using Microsoft.WSL.Containers;

namespace Wslc.Testcontainers.Runtime;

/// <summary>
/// Checks that the machine can run WSL containers using the official
/// <see cref="WslcService"/> entry point.
/// </summary>
internal static class WslcHost
{
    public static void EnsureAvailable()
    {
        WslPlatform.ThrowIfUnsupported();
        EnsureWslVersion();
        IReadOnlyList<Component> missing;
        try
        {
            missing = WslcService.GetMissingComponents();
        }
        catch (Exception exception)
        {
            throw new WslRuntimeException(
                "The WSL container runtime is not available. Install WSL 2.9.3 or newer with 'wsl --install' and retry.",
                exception);
        }

        if (missing.Count > 0)
        {
            throw new WslRuntimeException(
                $"The WSL container runtime is missing required components ({string.Join(", ", missing)}). " +
                "Run 'wsl --install' (or 'wsl --update') and retry.");
        }
    }

    public static string GetVersion()
    {
        try
        {
            var version = WslcService.GetVersion();
            return $"{version.Major}.{version.Minor}.{version.Revision}";
        }
        catch
        {
            return "unknown";
        }
    }

    private static void EnsureWslVersion()
    {
        uint major;
        uint minor;
        uint revision;
        try
        {
            var version = WslcService.GetVersion();
            major = version.Major;
            minor = version.Minor;
            revision = version.Revision;
        }
        catch (Exception exception)
        {
            throw new WslRuntimeException(
                "The WSL container runtime is not available. Install WSL 2.9.3 or newer with 'wsl --install' and retry.",
                exception);
        }

        if (!WslPlatform.IsWslVersionSupported(major, minor, revision))
        {
            throw new WslRuntimeException(
                $"WSL {major}.{minor}.{revision} is too old. Wslc.Testcontainers requires WSL {WslPlatform.MinimumWslVersion} or newer. " +
                "Run 'wsl --update' and retry.");
        }
    }
}
