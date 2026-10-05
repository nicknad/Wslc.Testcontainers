using System.Text;

namespace Wslc.Testcontainers.Internal;

/// <summary>Bounds captured and diagnostic text so failure reports stay small and reviewable.</summary>
internal static class TextTruncation
{
    /// <summary>Maximum UTF-8 bytes rendered by <c>WslReadinessException.Describe()</c>.</summary>
    internal const int MaxDescribeBytes = 64 * 1024;

    /// <summary>Maximum UTF-8 bytes for one captured output section.</summary>
    internal const int MaxSectionBytes = 8 * 1024;

    /// <summary>Maximum UTF-8 bytes for one captured line.</summary>
    internal const int MaxLineBytes = 4 * 1024;

    private const string LineSuffix = "...";

    /// <summary>Caps text to a UTF-8 byte budget, keeping a head and tail around an omitted marker.</summary>
    internal static string Cap(string value, int maxBytes)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        if (bytes.Length <= maxBytes)
        {
            return value;
        }

        if (maxBytes <= 0)
        {
            return string.Empty;
        }

        var headLength = maxBytes / 2;
        var tailStart = bytes.Length - (maxBytes - headLength);
        for (var attempt = 0; attempt < 8; attempt++)
        {
            headLength = Utf8HeadLength(bytes, Math.Min(headLength, bytes.Length));
            tailStart = Utf8TailStart(bytes, Math.Clamp(tailStart, headLength, bytes.Length));

            var omitted = bytes.Length - headLength - (bytes.Length - tailStart);
            var marker = FormattableString.Invariant($"... [{omitted} bytes omitted] ...");
            var overflow = headLength + Encoding.UTF8.GetByteCount(marker) + (bytes.Length - tailStart) - maxBytes;
            if (overflow <= 0)
            {
                return string.Concat(
                    Encoding.UTF8.GetString(bytes, 0, headLength),
                    marker,
                    Encoding.UTF8.GetString(bytes, tailStart, bytes.Length - tailStart));
            }

            var tailLength = bytes.Length - tailStart;
            if (overflow <= headLength)
            {
                headLength -= overflow;
            }
            else if (overflow - headLength <= tailLength)
            {
                tailStart += overflow - headLength;
                headLength = 0;
            }
            else
            {
                break;
            }
        }

        return Encoding.UTF8.GetString(bytes, 0, Utf8HeadLength(bytes, maxBytes));
    }

    /// <summary>Caps every line and then the joined section, preserving line breaks.</summary>
    internal static string CapLines(IEnumerable<string> lines, int maxLineBytes, int maxSectionBytes, string? separator = null)
    {
        var joiner = separator ?? "\n";
        var builder = new StringBuilder();
        var first = true;
        foreach (var line in lines)
        {
            if (!first)
            {
                builder.Append(joiner);
            }

            first = false;
            builder.Append(CapLine(line, maxLineBytes));
        }

        return Cap(builder.ToString(), maxSectionBytes);
    }

    private static string CapLine(string line, int maxBytes)
    {
        var bytes = Encoding.UTF8.GetBytes(line);
        if (bytes.Length <= maxBytes)
        {
            return line;
        }

        if (maxBytes <= LineSuffix.Length)
        {
            return LineSuffix[..Math.Max(0, maxBytes)];
        }

        var headLength = Utf8HeadLength(bytes, maxBytes - LineSuffix.Length);
        return string.Concat(Encoding.UTF8.GetString(bytes, 0, headLength), LineSuffix);
    }

    private static int Utf8HeadLength(ReadOnlySpan<byte> bytes, int length)
    {
        while (length > 0 && length < bytes.Length && (bytes[length] & 0xC0) == 0x80)
        {
            length--;
        }

        return length;
    }

    private static int Utf8TailStart(ReadOnlySpan<byte> bytes, int start)
    {
        while (start < bytes.Length && (bytes[start] & 0xC0) == 0x80)
        {
            start++;
        }

        return start;
    }
}
