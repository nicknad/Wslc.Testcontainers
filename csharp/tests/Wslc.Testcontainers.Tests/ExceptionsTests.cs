using System.Text;
using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class ExceptionsTests
{
    private const int LineBytes = 256 * 1024;
    private const int LineCount = 50;
    private const string Secret = "SECRET_BEYOND_THE_CAP";

    [Fact]
    public void Describe_caps_large_captured_output()
    {
        var exception = new WslReadinessException(
                "Timed out waiting for readiness",
                "TCP port 5432",
                TimeSpan.FromSeconds(5),
                BuildLogs())
            .WithDiagnostics("alpine:latest", "sleep infinity", 1, BuildStream(), BuildStream());

        var described = exception.Describe();

        Assert.True(described.Length <= TextTruncation.MaxDescribeBytes);
        Assert.True(Encoding.UTF8.GetByteCount(described) <= TextTruncation.MaxDescribeBytes);
        Assert.Contains("WSLC readiness failed", described);
        Assert.Contains("Image:        alpine:latest", described);
        Assert.Contains("Command:      sleep infinity", described);
        Assert.Contains("Expected:     TCP port 5432", described);
        Assert.Contains("Timeout:      5s", described);
        Assert.Contains("Exit code:    1", described);
        Assert.Contains("Last stdout:", described);
        Assert.Contains("Last stderr:", described);
        Assert.Contains("Recent logs:", described);
        Assert.Contains("bytes omitted", described);
        Assert.DoesNotContain(Secret, described);
    }

    [Fact]
    public void Describe_is_unchanged_for_small_diagnostics()
    {
        var line = new LogLine(LogSource.Stderr, "oops", DateTimeOffset.UnixEpoch);
        var exception = new WslReadinessException(
                "Timed out waiting for readiness",
                "TCP port 5432",
                TimeSpan.FromSeconds(5),
                new[] { line })
            .WithDiagnostics("alpine:latest", "sleep infinity", 3, "hello", "bad");

        var expected = string.Concat(
            "WSLC readiness failed", Environment.NewLine,
            Environment.NewLine,
            "Image:        alpine:latest", Environment.NewLine,
            "Command:      sleep infinity", Environment.NewLine,
            "Expected:     TCP port 5432", Environment.NewLine,
            "Timeout:      5s", Environment.NewLine,
            "Exit code:    3", Environment.NewLine,
            Environment.NewLine,
            "Last stdout:", Environment.NewLine,
            "hello", Environment.NewLine,
            Environment.NewLine,
            "Last stderr:", Environment.NewLine,
            "bad", Environment.NewLine,
            Environment.NewLine,
            "Recent logs:", Environment.NewLine,
            line.ToString(), Environment.NewLine);

        Assert.Equal(expected, exception.Describe());
    }

    [Fact]
    public void Describe_joins_log_entries_with_the_platform_separator()
    {
        var first = new LogLine(LogSource.Stdout, "first", DateTimeOffset.UnixEpoch);
        var second = new LogLine(LogSource.Stdout, "second", DateTimeOffset.UnixEpoch);
        var exception = new WslReadinessException(
            "Timed out waiting for readiness",
            "TCP port 5432",
            TimeSpan.FromSeconds(5),
            new[] { first, second });

        var described = exception.Describe();

        Assert.Contains(
            $"Recent logs:{Environment.NewLine}{first}{Environment.NewLine}{second}{Environment.NewLine}",
            described);
        Assert.DoesNotContain($"{first}\n{second}", described);
    }

    [Fact]
    public void Describe_keeps_the_no_command_hint()
    {
        var exception = new WslReadinessException("timed out", "TCP port 5432", TimeSpan.FromSeconds(5), Array.Empty<LogLine>());

        var described = exception.Describe();

        Assert.Contains("Command:      <none>", described);
        Assert.Contains("Hint:", described);
        Assert.Contains("ENTRYPOINT/CMD", described);
        Assert.True(described.Length <= TextTruncation.MaxDescribeBytes);
    }

    [Fact]
    public void Cap_returns_short_text_unchanged()
    {
        Assert.Equal("short", TextTruncation.Cap("short", 16));
        Assert.Equal(string.Empty, TextTruncation.Cap("abcdef", 0));
        Assert.Equal("ab", TextTruncation.Cap("abcdef", 2));
    }

    [Fact]
    public void Cap_never_splits_a_utf8_sequence()
    {
        var value = string.Concat(Enumerable.Repeat("é", 64));

        var capped = TextTruncation.Cap(value, 31);

        Assert.True(Encoding.UTF8.GetByteCount(capped) <= 31);
        Assert.DoesNotContain('\uFFFD', capped);
    }

    [Fact]
    public void Cap_keeps_the_head_and_tail_around_the_marker()
    {
        var value = new string('a', 1024) + "middle" + new string('b', 1024);

        var capped = TextTruncation.Cap(value, 512);

        Assert.True(Encoding.UTF8.GetByteCount(capped) <= 512);
        Assert.Contains("bytes omitted", capped);
        Assert.StartsWith(new string('a', 16), capped, StringComparison.Ordinal);
        Assert.EndsWith(new string('b', 16), capped, StringComparison.Ordinal);
    }

    [Fact]
    public void CapLines_caps_each_line()
    {
        var capped = TextTruncation.CapLines(new[] { new string('x', 8192) }, 4096, 8192);

        Assert.Equal(4096, capped.Length);
        Assert.EndsWith("...", capped, StringComparison.Ordinal);
    }

    private static string BuildStream()
    {
        var builder = new StringBuilder(LineCount * (LineBytes + 1));
        for (var i = 0; i < LineCount; i++)
        {
            var filler = (char)('a' + (i % 26));
            if (i == LineCount / 2)
            {
                builder.Append(filler, LineBytes / 2);
                builder.Append(Secret);
                builder.Append(filler, LineBytes / 2 - Secret.Length);
            }
            else
            {
                builder.Append(filler, LineBytes);
            }

            builder.Append('\n');
        }

        return builder.ToString();
    }

    private static List<LogLine> BuildLogs()
    {
        var logs = new List<LogLine>(LineCount);
        for (var i = 0; i < LineCount; i++)
        {
            var filler = (char)('a' + (i % 26));
            var text = i == LineCount / 2
                ? new string(filler, LineBytes / 2) + Secret + new string(filler, LineBytes / 2 - Secret.Length)
                : new string(filler, LineBytes);
            logs.Add(new LogLine(LogSource.Stdout, text, DateTimeOffset.UnixEpoch));
        }

        return logs;
    }
}
