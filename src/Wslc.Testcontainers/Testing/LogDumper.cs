namespace Wslc.Testcontainers.Testing;

/// <summary>
/// Dumps bounded log output to any sink (xUnit <c>ITestOutputHelper.WriteLine</c>,
/// console, file). Takes <c>Action&lt;string&gt;</c> so the library does not
/// depend on any test framework.
/// </summary>
public static class LogDumper
{
    /// <summary>
    /// Dumps up to <paramref name="maxLines"/> lines from a live stream, then stops.
    /// The cap is what bounds the read: log streams are otherwise infinite.
    /// </summary>
    public static async Task DumpAsync(
        IAsyncEnumerable<LogLine> logs,
        Action<string> writeLine,
        int maxLines = 100,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(logs);
        ArgumentNullException.ThrowIfNull(writeLine);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxLines);

        var count = 0;
        await foreach (var line in logs.WithCancellation(cancellationToken).ConfigureAwait(false))
        {
            writeLine(line.ToString());
            if (++count >= maxLines)
            {
                break;
            }
        }
    }
}
