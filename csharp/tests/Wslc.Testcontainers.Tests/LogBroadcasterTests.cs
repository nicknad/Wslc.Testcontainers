using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class LogBroadcasterTests
{
    [Fact]
    public void Snapshot_returns_published_lines()
    {
        var broadcaster = new LogBroadcaster();
        broadcaster.Publish(LogLine.Diagnostic("one"));
        broadcaster.Publish(LogLine.Diagnostic("two"));

        var snapshot = broadcaster.Snapshot();

        Assert.Equal(2, snapshot.Count);
        Assert.Equal("one", snapshot[0].Text);
    }

    [Fact]
    public async Task Subscribers_receive_history_and_live_lines()
    {
        var broadcaster = new LogBroadcaster();
        broadcaster.Publish(LogLine.Diagnostic("history"));

        using var subscription = broadcaster.Subscribe();
        broadcaster.Publish(LogLine.Diagnostic("live"));
        broadcaster.Complete();

        var lines = new List<string>();
        await foreach (var line in subscription.Reader.ReadAllAsync(TestContext.Current.CancellationToken))
        {
            lines.Add(line.Text);
        }

        Assert.Equal(new[] { "history", "live" }, lines);
    }

    [Fact]
    public async Task Completed_broadcasters_replay_history_when_streamed()
    {
        var broadcaster = new LogBroadcaster();
        broadcaster.Publish(LogLine.Diagnostic("first"));
        broadcaster.Publish(LogLine.Diagnostic("second"));
        broadcaster.Complete();

        var lines = new List<string>();
        await foreach (var line in broadcaster.StreamAsync(TestContext.Current.CancellationToken))
        {
            lines.Add(line.Text);
        }

        Assert.Equal(new[] { "first", "second" }, lines);
    }

    [Fact]
    public async Task Completed_broadcaster_replays_only_the_newest_lines_when_streamed()
    {
        var broadcaster = new LogBroadcaster();
        for (var index = 0; index < 1_050; index++)
        {
            broadcaster.Publish(LogLine.Diagnostic($"line {index}"));
        }

        broadcaster.Complete();

        var lines = new List<string>();
        await foreach (var line in broadcaster.StreamAsync(TestContext.Current.CancellationToken))
        {
            lines.Add(line.Text);
        }

        Assert.Equal(1_000, lines.Count);
        Assert.Equal("line 50", lines[0]);
        Assert.Equal("line 1049", lines[^1]);
    }

    [Fact]
    public void History_is_bounded()
    {
        var broadcaster = new LogBroadcaster();
        for (var index = 0; index < 10_050; index++)
        {
            broadcaster.Publish(LogLine.Diagnostic($"line {index}"));
        }

        var snapshot = broadcaster.Snapshot();

        Assert.Equal(10_000, snapshot.Count);
        Assert.Equal("line 50", snapshot[0].Text);
    }

    [Fact]
    public void Snapshot_is_cached_until_the_next_publish()
    {
        var broadcaster = new LogBroadcaster();
        broadcaster.Publish(LogLine.Diagnostic("one"));

        var first = broadcaster.Snapshot();
        Assert.Same(first, broadcaster.Snapshot());

        broadcaster.Publish(LogLine.Diagnostic("two"));
        var second = broadcaster.Snapshot();

        Assert.NotSame(first, second);
        Assert.Equal(2, second.Count);
        Assert.Equal("two", second[1].Text);
    }

    [Fact]
    public async Task Slow_subscribers_drop_the_oldest_lines_when_full()
    {
        var broadcaster = new LogBroadcaster();
        using var subscription = broadcaster.Subscribe();

        for (var index = 0; index < 1_050; index++)
        {
            broadcaster.Publish(LogLine.Diagnostic($"line {index}"));
        }

        broadcaster.Complete();

        var lines = new List<string>();
        await foreach (var line in subscription.Reader.ReadAllAsync(TestContext.Current.CancellationToken))
        {
            lines.Add(line.Text);
        }

        Assert.Equal(1_000, lines.Count);
        Assert.Equal("line 50", lines[0]);
        Assert.Equal("line 1049", lines[^1]);
    }
}
