using System.Runtime.InteropServices;

namespace Wslc.Testcontainers;

/// <summary>
/// Windows-only platform guard for WSLC. The library runs exclusively on
/// Windows 10 build 19041+ (x64/ARM64) with WSL container support; every public
/// entry point fails fast with <see cref="PlatformNotSupportedException"/> elsewhere
/// instead of surfacing obscure loader or COM errors.
/// </summary>
public static class WslPlatform
{
    /// <summary>Minimum supported Windows build (Windows 10, version 2004).</summary>
    public static readonly Version MinimumWindowsVersion = new(10, 0, 19041);

    /// <summary>Minimum supported WSL version with container support.</summary>
    public static readonly Version MinimumWslVersion = new(2, 9, 3);

    /// <summary>Gets a value indicating whether the current machine can run WSLC.</summary>
    public static bool IsSupported =>
        OperatingSystem.IsWindowsVersionAtLeast(10, 0, 19041) &&
        RuntimeInformation.ProcessArchitecture is Architecture.X64 or Architecture.Arm64;

    /// <summary>Gets a short description of the current OS and architecture for diagnostics.</summary>
    public static string DescribeCurrent() =>
        $"{RuntimeInformation.OSDescription.Trim()} ({RuntimeInformation.ProcessArchitecture})";

    /// <summary>
    /// Throws <see cref="PlatformNotSupportedException"/> when the current machine cannot run WSLC.
    /// Call at the top of every public entry point (<c>Build()</c>, <c>StartAsync()</c>, host init).
    /// </summary>
    public static void ThrowIfUnsupported()
    {
        if (IsSupported)
        {
            return;
        }

        throw new PlatformNotSupportedException(
            $"Wslc.Testcontainers requires Windows 10 build 19041+ (x64/ARM64) with WSL {MinimumWslVersion} or newer. " +
            $"Current: {DescribeCurrent()}. " +
            $"Run 'wsl --install' (or 'wsl --update') on a supported Windows host and retry.");
    }

    /// <summary>Returns true when a WSL version meets the minimum container-support requirement.</summary>
    internal static bool IsWslVersionSupported(Version version)
    {
        ArgumentNullException.ThrowIfNull(version);
        var patch = version.Build >= 0 ? version.Build : version.Revision;
        return IsWslVersionSupported((uint)version.Major, (uint)version.Minor, (uint)Math.Max(patch, 0));
    }

    /// <summary>Returns true when a WSL (major.minor.patch) version meets the minimum requirement.</summary>
    internal static bool IsWslVersionSupported(uint major, uint minor, uint patch)
    {
        if (major != MinimumWslVersion.Major)
        {
            return major > MinimumWslVersion.Major;
        }

        if (minor != MinimumWslVersion.Minor)
        {
            return minor > MinimumWslVersion.Minor;
        }

        var minimumPatch = MinimumWslVersion.Build >= 0 ? MinimumWslVersion.Build : MinimumWslVersion.Revision;
        return patch >= (uint)minimumPatch;
    }
}
