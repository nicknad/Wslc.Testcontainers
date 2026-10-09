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

/// <summary>
/// A single captured log line. Immutable: the source, text and timestamp are set once at
/// construction and cannot be reassigned or mutated with <c>with</c>.
/// </summary>
public sealed record LogLine
{
    /// <summary>Initializes a log line.</summary>
    /// <param name="source">The stream the line originated from.</param>
    /// <param name="text">The line content.</param>
    /// <param name="timestamp">UTC timestamp of the line.</param>
    public LogLine(LogSource source, string text, DateTimeOffset timestamp)
    {
        Source = source;
        Text = text;
        Timestamp = timestamp;
    }

    /// <summary>The stream the line originated from.</summary>
    public LogSource Source { get; }

    /// <summary>The line content.</summary>
    public string Text { get; }

    /// <summary>UTC timestamp of the line.</summary>
    public DateTimeOffset Timestamp { get; }

    /// <summary>Deconstructs the line into its source, text and timestamp.</summary>
    public void Deconstruct(out LogSource source, out string text, out DateTimeOffset timestamp)
    {
        source = Source;
        text = Text;
        timestamp = Timestamp;
    }

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
