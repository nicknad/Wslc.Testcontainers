namespace Wslc.Testcontainers;

/// <summary>Base exception for all WSLC failures.</summary>
public class WslcException(string message, Exception? innerException = null)
    : Exception(message, innerException);

/// <summary>WSL itself is missing, disabled or misconfigured.</summary>
public sealed class WslRuntimeException(string message, Exception? innerException = null)
    : WslcException(message, innerException)
{
    internal static WslRuntimeException FromHResult(string context, Exception exception) =>
        new($"{context} (HRESULT 0x{exception.HResult:X8}: {exception.Message})", exception);
}

/// <summary>Creating or preparing the WSL environment failed.</summary>
public sealed class WslProvisioningException(string message, Exception? innerException = null)
    : WslcException(message, innerException);

/// <summary>A command or process inside the environment failed.</summary>
public sealed class WslProcessException(string message, Exception? innerException = null)
    : WslcException(message, innerException);

/// <summary>An operation exceeded its configured timeout.</summary>
public class WslTimeoutException(string message, Exception? innerException = null)
    : WslcException(message, innerException);

/// <summary>A readiness (wait) strategy did not become satisfied in time.</summary>
public sealed class WslReadinessException(
    string message,
    string expectedCondition,
    TimeSpan timeout,
    IReadOnlyList<LogLine> logs)
    : WslTimeoutException(message)
{
    /// <summary>The condition that was awaited.</summary>
    public string ExpectedCondition { get; } = expectedCondition;

    /// <summary>The configured timeout.</summary>
    public TimeSpan Timeout { get; } = timeout;

    /// <summary>Recent log lines captured when the wait failed.</summary>
    public IReadOnlyList<LogLine> Logs { get; } = logs;

    /// <summary>The container image reference.</summary>
    public string? Image { get; internal set; }

    /// <summary>The configured command, when any.</summary>
    public string? Command { get; internal set; }

    /// <summary>The exit code of the main process, when it had already exited.</summary>
    public int? ExitCode { get; internal set; }

    /// <summary>Captured standard output.</summary>
    public string? Stdout { get; internal set; }

    /// <summary>Captured standard error.</summary>
    public string? Stderr { get; internal set; }

    /// <summary>Renders the full diagnostic report required for failed readiness.</summary>
    public string Describe()
    {
        var builder = new System.Text.StringBuilder(512);
        builder.AppendLine("WSLC readiness failed");
        builder.AppendLine();
        builder.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Image:        {Image ?? "<unknown>"}");
        builder.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Command:      {Command ?? "<none>"}");
        if (Command is null)
        {
            builder.AppendLine("Hint:         no init command was configured, so only a keep-alive shell is running.");
            builder.AppendLine("              WSLC never runs the image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or use a module builder.");
        }

        builder.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Expected:     {ExpectedCondition}");
        builder.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Timeout:      {Timeout.TotalSeconds:0.###}s");

        if (ExitCode is int exitCode)
        {
            builder.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Exit code:    {exitCode}");
        }

        if (!string.IsNullOrWhiteSpace(Stdout))
        {
            builder.AppendLine();
            builder.AppendLine("Last stdout:");
            builder.AppendLine(Stdout);
        }

        if (!string.IsNullOrWhiteSpace(Stderr))
        {
            builder.AppendLine();
            builder.AppendLine("Last stderr:");
            builder.AppendLine(Stderr);
        }

        if (Logs.Count > 0)
        {
            builder.AppendLine();
            builder.AppendLine("Recent logs:");
            foreach (var line in Logs)
            {
                builder.AppendLine(line.ToString());
            }
        }

        return builder.ToString();
    }
}

/// <summary>Port forwarding or address resolution failed.</summary>
public sealed class WslNetworkException(string message, Exception? innerException = null)
    : WslcException(message, innerException);

/// <summary>Cleanup of a WSLC environment failed.</summary>
public sealed class WslCleanupException(string message, Exception? innerException = null)
    : WslcException(message, innerException);
