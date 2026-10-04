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

    /// <summary>Maximum memory limit in megabytes accepted by <c>WithMemoryMB</c> (1 TiB).</summary>
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

    /// <summary>Maximum number of session VHD volumes.</summary>
    public const int MaxSessionVolumes = 64;

    /// <summary>Maximum number of readiness wait strategies, including strategies combined through And.</summary>
    public const int MaxWaitStrategies = 16;

    /// <summary>Maximum session volume size in bytes (1 TiB).</summary>
    public const ulong MaxSessionVolumeBytes = 1UL << 40;

    /// <summary>Maximum tarball size in bytes: the smaller of 1 TiB and the session VHD cap.</summary>
    public const ulong MaxTarballBytes = MaxSessionVolumeBytes;

    /// <summary>Maximum startup timeout.</summary>
    public static readonly TimeSpan MaxStartupTimeout = TimeSpan.FromHours(24);

    /// <summary>Maximum exec timeout.</summary>
    public static readonly TimeSpan MaxExecTimeout = TimeSpan.FromHours(24);

    public static void RequireCpuCount(uint cpuCount)
    {
        if (cpuCount == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(cpuCount), cpuCount, "CPU count must be positive.");
        }

        if (cpuCount > MaxCpuCount)
        {
            throw new ArgumentOutOfRangeException(
                nameof(cpuCount), cpuCount, $"CPU count {cpuCount} exceeds the maximum of {MaxCpuCount}.");
        }
    }

    public static void RequireMemoryMB(uint megabytes)
    {
        if (megabytes == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(megabytes), megabytes, "Memory limit must be positive.");
        }

        if (megabytes > MaxMemoryMB)
        {
            throw new ArgumentOutOfRangeException(
                nameof(megabytes),
                megabytes,
                $"Memory limit {megabytes} MB exceeds the maximum of {MaxMemoryMB} MB (1 TiB).");
        }
    }

    public static void RequireSessionVolumeSize(ulong sizeBytes)
    {
        if (sizeBytes == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sizeBytes), sizeBytes, "Session volume size must be positive.");
        }

        if (sizeBytes > MaxSessionVolumeBytes)
        {
            throw new ArgumentOutOfRangeException(
                nameof(sizeBytes),
                sizeBytes,
                $"Session volume size {sizeBytes} bytes exceeds the maximum of {MaxSessionVolumeBytes} bytes (1 TiB).");
        }
    }

    public static void RequireStartupTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), timeout, "Timeout must be positive.");
        }

        if (timeout > MaxStartupTimeout)
        {
            throw new ArgumentOutOfRangeException(
                nameof(timeout),
                timeout,
                $"Startup timeout {timeout.TotalSeconds:0.###}s exceeds the maximum of {MaxStartupTimeout.TotalHours:0.###}h.");
        }
    }

    public static void RequireExecTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), timeout, "Exec timeout must be positive.");
        }

        if (timeout > MaxExecTimeout)
        {
            throw new ArgumentOutOfRangeException(
                nameof(timeout),
                timeout,
                $"Exec timeout {timeout.TotalSeconds:0.###}s exceeds the maximum of {MaxExecTimeout.TotalHours:0.###}h.");
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
            throw new WslcException(
                $"Tarball '{tarballPath}' is {length} bytes; the maximum is {MaxTarballBytes} bytes (1 TiB).");
        }
    }

    public static void RequireCount(int count, int maximum, string description)
    {
        if (count > maximum)
        {
            throw new WslcException($"Too many {description}: {count} configured, but the maximum is {maximum}.");
        }
    }

    public static void RequireWaitStrategyCount(int count)
    {
        if (count > MaxWaitStrategies)
        {
            throw new WslcException(
                $"Too many wait strategies: {count} configured or combined, but the maximum is {MaxWaitStrategies}.");
        }
    }
}
