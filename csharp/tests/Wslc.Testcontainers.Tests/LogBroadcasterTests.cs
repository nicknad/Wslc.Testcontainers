using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class LogBroadcasterTests
{
    [Fact]
    public void Publish_keeps_normal_lines_unchanged()
    {
        var broadcaster = new LogBroadcaster();
        var timestamp = DateTimeOffset.UnixEpoch;

        broadcaster.Publish(new LogLine(LogSource.Stdout, "hello", timestamp));

        var line = Assert.Single(broadcaster.Snapshot());
        Assert.Equal(LogSource.Stdout, line.Source);
        Assert.Equal("hello", line.Text);
        Assert.Equal(timestamp, line.Timestamp);
    }

    [Fact]
    public void Publish_caps_oversized_lines()
    {
        var broadcaster = new LogBroadcaster();
        var text = new string('x', LogBroadcaster.MaxLineBytes * 4);

        broadcaster.Publish(new LogLine(LogSource.Stdout, text, DateTimeOffset.UnixEpoch));

        var line = Assert.Single(broadcaster.Snapshot());
        Assert.True(line.Text.Length <= LogBroadcaster.MaxLineBytes, $"Line was {line.Text.Length} chars.");
        Assert.Contains("bytes omitted", line.Text, StringComparison.Ordinal);
        Assert.StartsWith("xxx", line.Text, StringComparison.Ordinal);
        Assert.EndsWith("xxx", line.Text, StringComparison.Ordinal);
    }
}
