using System.Text;

namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Central caps for builder-provided values. Single-value caps fail fast in the
/// <c>With...</c> setters; aggregate caps are enforced by <c>Build</c> because those values
/// accumulate across calls. The caps bound the resource sizes the WSLC runtime accepts and
/// the startup work a container requests.
/// </summary>
internal static class BuilderLimits
{
    /// <summary>Maximum CPU count accepted by <c>WithCpuCount</c>.</summary>
    public const uint MaxCpuCount = 64;

    /// <summary>Maximum memory limit in megabytes accepted by <c>WithMemoryMegabytes</c> (1 TiB).</summary>
    public const uint MaxMemoryMB = 1024 * 1024;

    /// <summary>Maximum number of command arguments.</summary>
    public const int MaxCommandArguments = 1000;

    /// <summary>Maximum number of environment variables.</summary>
    public const int MaxEnvironmentVariables = 1000;

    /// <summary>Maximum size of one environment value in UTF-8 bytes (128 KiB).</summary>
    public const int MaxEnvironmentValueBytes = 128 * 1024;

    /// <summary>Maximum number of host files copied during startup.</summary>
    public const int MaxFileCopies = 64;

    /// <summary>Maximum number of host directory mounts.</summary>
    public const int MaxVolumeMounts = 64;

    /// <summary>Maximum number of scratch VHD volumes.</summary>
    public const int MaxScratchVolumes = 64;

    /// <summary>Maximum number of readiness wait strategies, including strategies combined through And.</summary>
    public const int MaxWaitStrategies = 16;

    /// <summary>Maximum scratch volume size in bytes (1 TiB).</summary>
    public const ulong MaxScratchVolumeBytes = 1UL << 40;

    /// <summary>Maximum tarball size in bytes: the smaller of 1 TiB and the session VHD cap.</summary>
    public const ulong MaxTarballBytes = MaxScratchVolumeBytes;

    /// <summary>Maximum startup timeout.</summary>
    public static readonly TimeSpan MaxStartupTimeout = TimeSpan.FromHours(24);

    /// <summary>Maximum exec timeout.</summary>
    public static readonly TimeSpan MaxExecTimeout = TimeSpan.FromHours(24);

    public static void RequireCpuCount(uint cpuCount) =>
        RequirePositiveAtMost(cpuCount, MaxCpuCount, nameof(cpuCount), "CPU count", $"{MaxCpuCount}");

    public static void RequireMemoryMB(uint megabytes) =>
        RequirePositiveAtMost(megabytes, MaxMemoryMB, nameof(megabytes), "Memory limit", $"{MaxMemoryMB} MB (1 TiB)");

    public static void RequireScratchVolumeSize(ulong sizeBytes) =>
        RequirePositiveAtMost(sizeBytes, MaxScratchVolumeBytes, nameof(sizeBytes), "Scratch volume size",
                              $"{MaxScratchVolumeBytes} bytes (1 TiB)");

    public static void RequireStartupTimeout(TimeSpan timeout) =>
        RequirePositiveAtMost(timeout, MaxStartupTimeout, nameof(timeout), "Startup timeout",
                              $"{MaxStartupTimeout.TotalHours:0.###}h");

    public static void RequireExecTimeout(TimeSpan timeout) =>
        RequirePositiveAtMost(timeout, MaxExecTimeout, nameof(timeout), "Exec timeout",
                              $"{MaxExecTimeout.TotalHours:0.###}h");

    private static void RequirePositiveAtMost<T>(T value, T maximum, string parameterName, string description,
                                                 string maximumText)
        where T : struct, IComparable<T>
    {
        if (value.CompareTo(default) <= 0)
        {
            throw new ArgumentOutOfRangeException(parameterName, value, $"{description} must be positive.");
        }

        if (value.CompareTo(maximum) > 0)
        {
            throw new ArgumentOutOfRangeException(parameterName, value,
                                                  $"{description} {value} exceeds the maximum of {maximumText}.");
        }
    }

    public static void RequireEnvironmentValue(string variableName, string value, string parameterName)
    {
        var byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount > MaxEnvironmentValueBytes)
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                byteCount,
                $"Environment variable '{variableName}' value is {byteCount} bytes; the maximum is {MaxEnvironmentValueBytes} bytes (128 KiB).");
        }
    }

    public static void RequireTarballSize(string tarballPath, long length)
    {
        if ((ulong)length > MaxTarballBytes)
        {
            throw new WslException(
                $"Tarball '{tarballPath}' is {length} bytes; the maximum is {MaxTarballBytes} bytes (1 TiB).");
        }
    }

    public static void RequireCount(int count, int maximum, string description)
    {
        if (count > maximum)
        {
            throw new WslException($"Too many {description}: {count} configured, but the maximum is {maximum}.");
        }
    }

    public static void RequireWaitStrategyCount(int count)
    {
        if (count > MaxWaitStrategies)
        {
            throw new WslException(
                $"Too many wait strategies: {count} configured or combined, but the maximum is {MaxWaitStrategies}.");
        }
    }

    /// <summary>Adds two timeouts, saturating at <see cref="TimeSpan.MaxValue"/> instead of overflowing.</summary>
    public static TimeSpan SaturatingAdd(TimeSpan left, TimeSpan right) =>
        right > TimeSpan.MaxValue - left ? TimeSpan.MaxValue : left + right;
}
