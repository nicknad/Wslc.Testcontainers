namespace Wslc.Testcontainers.Internal;

/// <summary>Shared helpers for bounded log snapshots.</summary>
internal static class LogHelpers
{
    public static IReadOnlyList<LogLine> TakeLast(IReadOnlyList<LogLine> logs, int maxLines)
    {
        if (logs.Count <= maxLines)
        {
            return logs;
        }

        var result = new LogLine[maxLines];
        var start = logs.Count - maxLines;
        for (var i = 0; i < maxLines; i++)
        {
            result[i] = logs[start + i];
        }

        return result;
    }

    public static string? JoinLast(IReadOnlyList<LogLine> logs, LogSource source, int maxLines)
    {
        Queue<string>? selected = null;
        for (var i = 0; i < logs.Count; i++)
        {
            if (logs[i].Source != source)
            {
                continue;
            }

            selected ??= new Queue<string>(maxLines + 1);
            selected.Enqueue(logs[i].Text);
            if (selected.Count > maxLines)
            {
                selected.Dequeue();
            }
        }

        return selected is null ? null : string.Join(Environment.NewLine, selected);
    }
}
