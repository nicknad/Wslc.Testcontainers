namespace Wslc.Testcontainers.Testing;

/// <summary>
/// Dumps bounded log output to any sink (xUnit <c>ITestOutputHelper.WriteLine</c>,
/// console, file). Takes <c>Action&lt;string&gt;</c> so the library does not
/// depend on any test framework.
/// </summary>
public static class LogDumper
{
    /// <summary>
    /// Dumps the head of a live stream — up to <paramref name="maxLines"/> lines, oldest first —
    /// then stops. The stream replays retained history oldest-first, so this returns the
    /// <b>oldest</b> lines, not the latest; use <see cref="IWslContainer.GetRecentLogs"/> for a
    /// bounded tail snapshot. The cap is what bounds the read: log streams are otherwise infinite.
    /// </summary>
    public static async Task DumpHeadAsync(
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
