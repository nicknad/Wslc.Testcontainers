namespace Wslc.Testcontainers;

/// <summary>Identifies the origin of a log line.</summary>
public enum LogSource
{
    /// <summary>Standard output of a process.</summary>
    Stdout,

    /// <summary>Standard error of a process.</summary>
    Stderr,

    /// <summary>Diagnostic output produced by WSLC itself.</summary>
    System,
}

/// <summary>A single captured log line.</summary>
/// <param name="Source">The stream the line originated from.</param>
/// <param name="Text">The line content.</param>
/// <param name="Timestamp">UTC timestamp of the line.</param>
public sealed record LogLine(LogSource Source, string Text, DateTimeOffset Timestamp)
{
    /// <summary>Creates a WSLC diagnostic line.</summary>
    internal static LogLine Diagnostic(string text) => new(LogSource.System, text, DateTimeOffset.UtcNow);

    /// <inheritdoc />
    public override string ToString() =>
        $"{Timestamp:HH:mm:ss.fff} [{SourceName(Source)}] {Text}";

    private static string SourceName(LogSource source) => source switch
    {
        LogSource.Stdout => "stdout",
        LogSource.Stderr => "stderr",
        _ => "system",
    };
}
