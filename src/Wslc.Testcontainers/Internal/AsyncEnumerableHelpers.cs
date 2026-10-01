using System.Runtime.CompilerServices;

namespace Wslc.Testcontainers.Internal;

internal static class AsyncEnumerableHelpers
{
    public static async IAsyncEnumerable<string> TextLines(
        this IAsyncEnumerable<LogLine> source,
        LogSource sourceFilter,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await foreach (var line in source.WithCancellation(cancellationToken).ConfigureAwait(false))
        {
            if (line.Source == sourceFilter)
            {
                yield return line.Text;
            }
        }
    }
}
