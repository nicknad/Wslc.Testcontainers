using Wslc.Testcontainers.Internal;

namespace Wslc.Testcontainers;

/// <summary>
/// Base exception for all WSLC failures, including invalid builder configuration reported by
/// <c>Build()</c> and operations attempted before <c>StartAsync()</c>.
/// </summary>
public class WslException(string message, Exception? innerException = null)
    : Exception(message, innerException);

/// <summary>WSL itself is missing, disabled or misconfigured.</summary>
public sealed class WslRuntimeException(string message, Exception? innerException = null)
    : WslException(message, innerException)
{
    internal static WslRuntimeException FromHResult(string context, Exception exception) =>
        new($"{context} (HRESULT 0x{exception.HResult:X8}: {exception.Message})", exception);
}

/// <summary>Creating or preparing the WSL environment failed.</summary>
public sealed class WslProvisioningException(string message, Exception? innerException = null)
    : WslException(message, innerException);

/// <summary>A command or process inside the environment failed.</summary>
public sealed class WslProcessException(string message, Exception? innerException = null)
    : WslException(message, innerException);

/// <summary>An operation exceeded its configured timeout.</summary>
public class WslTimeoutException(string message, Exception? innerException = null)
    : WslException(message, innerException);

/// <summary>A readiness (wait) strategy did not become satisfied in time.</summary>
public sealed class WslReadinessException : WslTimeoutException
{
    /// <summary>Creates a readiness failure without container diagnostics.</summary>
    public WslReadinessException(
        string message,
        string expectedCondition,
        TimeSpan timeout,
        IReadOnlyList<LogLine> logs)
        : this(message, expectedCondition, timeout, logs, image: null, command: null, exitCode: null, stdout: null, stderr: null)
    {
    }

    private WslReadinessException(
        string message,
        string expectedCondition,
        TimeSpan timeout,
        IReadOnlyList<LogLine> logs,
        string? image,
        string? command,
        int? exitCode,
        string? stdout,
        string? stderr)
        : base(message)
    {
        ExpectedCondition = expectedCondition;
        Timeout = timeout;
        Logs = logs ?? Array.Empty<LogLine>();
        Image = image;
        Command = command;
        ExitCode = exitCode;
        Stdout = stdout;
        Stderr = stderr;
    }

    /// <summary>The condition that was awaited.</summary>
    public string ExpectedCondition { get; }

    /// <summary>The configured timeout.</summary>
    public TimeSpan Timeout { get; }

    /// <summary>Recent log lines captured when the wait failed.</summary>
    /// <remarks>
    /// Captured process output is untrusted, may contain secrets, and is truncated by
    /// <see cref="Describe"/>.
    /// </remarks>
    public IReadOnlyList<LogLine> Logs { get; }

    /// <summary>The container image reference.</summary>
    public string? Image { get; }

    /// <summary>The configured command, when any.</summary>
    public string? Command { get; }

    /// <summary>The exit code of the main process, when it had already exited.</summary>
    public int? ExitCode { get; }

    /// <summary>Captured standard output.</summary>
    /// <remarks>
    /// Captured process output is untrusted, may contain secrets, and is truncated by
    /// <see cref="Describe"/>.
    /// </remarks>
    public string? Stdout { get; }

    /// <summary>Captured standard error.</summary>
    /// <remarks>
    /// Captured process output is untrusted, may contain secrets, and is truncated by
    /// <see cref="Describe"/>.
    /// </remarks>
    public string? Stderr { get; }

    /// <summary>
    /// Returns a copy with container diagnostics filled in. The container enriches readiness
    /// failures before they reach consumers, so the thrown instance is fully populated.
    /// </summary>
    internal WslReadinessException WithDiagnostics(
        string? image,
        string? command,
        int? exitCode,
        string? stdout,
        string? stderr) =>
        new(
            Message,
            ExpectedCondition,
            Timeout,
            Logs,
            image ?? Image,
            command ?? Command,
            exitCode ?? ExitCode,
            stdout ?? Stdout,
            stderr ?? Stderr);

    /// <summary>Renders the full diagnostic report required for failed readiness.</summary>
    /// <remarks>
    /// Captured output is untrusted, may contain secrets, and can be arbitrarily large. Each
    /// captured section is capped at 8 KiB (head and tail around an omitted marker), each line at
    /// 4 KiB, and the whole report at 64 KiB. The header is never truncated.
    /// </remarks>
    public string Describe()
    {
        var header = new System.Text.StringBuilder(512);
        header.AppendLine("WSLC readiness failed");
        header.AppendLine();
        header.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Image:        {Image ?? "<unknown>"}");
        header.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Command:      {Command ?? "<none>"}");
        if (Command is null)
        {
            header.AppendLine("Hint:         no init command was configured, so only a keep-alive shell is running.");
            header.AppendLine("              WSLC never runs the image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or use a module builder.");
        }

        header.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Expected:     {ExpectedCondition}");
        header.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Timeout:      {Timeout.TotalSeconds:0.###}s");

        if (ExitCode is int exitCode)
        {
            header.AppendLine(System.Globalization.CultureInfo.InvariantCulture, $"Exit code:    {exitCode}");
        }

        var body = new System.Text.StringBuilder(512);
        if (!string.IsNullOrWhiteSpace(Stdout))
        {
            body.AppendLine();
            body.AppendLine("Last stdout:");
            body.AppendLine(TextTruncation.CapLines(Stdout.Split('\n'), TextTruncation.MaxLineBytes, TextTruncation.MaxSectionBytes));
        }

        if (!string.IsNullOrWhiteSpace(Stderr))
        {
            body.AppendLine();
            body.AppendLine("Last stderr:");
            body.AppendLine(TextTruncation.CapLines(Stderr.Split('\n'), TextTruncation.MaxLineBytes, TextTruncation.MaxSectionBytes));
        }

        if (Logs.Count > 0)
        {
            body.AppendLine();
            body.AppendLine("Recent logs:");
            body.AppendLine(TextTruncation.CapLines(
                Logs.Select(line => line.ToString()),
                TextTruncation.MaxLineBytes,
                TextTruncation.MaxSectionBytes,
                Environment.NewLine));
        }

        var headerText = header.ToString();
        var remaining = Math.Max(0, TextTruncation.MaxDescribeBytes - System.Text.Encoding.UTF8.GetByteCount(headerText));
        var bodyText = body.ToString();
        if (System.Text.Encoding.UTF8.GetByteCount(bodyText) > remaining)
        {
            bodyText = TextTruncation.Cap(bodyText, remaining);
        }

        return headerText + bodyText;
    }
}

/// <summary>Port forwarding or address resolution failed.</summary>
public sealed class WslNetworkException(string message, Exception? innerException = null)
    : WslException(message, innerException);

/// <summary>Cleanup of a WSLC environment failed.</summary>
public sealed class WslCleanupException(string message, Exception? innerException = null)
    : WslException(message, innerException);
