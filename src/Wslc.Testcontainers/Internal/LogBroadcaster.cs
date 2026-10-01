using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Wslc.Testcontainers.Internal;

/// <summary>Thread-safe fan-out log buffer with bounded history.</summary>
internal sealed class LogBroadcaster
{
    private const int MaxHistory = 10_000;
    private readonly object _gate = new();
    private readonly LinkedList<LogLine> _history = new();
    private readonly List<Channel<LogLine>> _subscribers = new();
    private bool _completed;

    public void Publish(LogLine line)
    {
        lock (_gate)
        {
            if (_completed)
            {
                return;
            }

            _history.AddLast(line);
            while (_history.Count > MaxHistory)
            {
                _history.RemoveFirst();
            }

            foreach (var subscriber in _subscribers)
            {
                _ = subscriber.Writer.TryWrite(line);
            }
        }
    }

    public IReadOnlyList<LogLine> Snapshot(int maxLines = int.MaxValue)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(maxLines);

        lock (_gate)
        {
            if (maxLines >= _history.Count)
            {
                return _history.ToArray();
            }

            var result = new LogLine[maxLines];
            var node = _history.Last;
            for (var i = maxLines - 1; i >= 0; i--)
            {
                result[i] = node!.Value;
                node = node.Previous;
            }

            return result;
        }
    }

    public LogSubscription Subscribe(int historyLines = 0)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(historyLines);

        var channel = Channel.CreateUnbounded<LogLine>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
        });

        lock (_gate)
        {
            if (historyLines > 0 && _history.Count > 0)
            {
                var take = Math.Min(historyLines, _history.Count);
                var node = _history.First;
                var skip = _history.Count - take;
                for (var i = 0; i < skip; i++)
                {
                    node = node!.Next;
                }

                for (var i = 0; i < take; i++)
                {
                    _ = channel.Writer.TryWrite(node!.Value);
                    node = node.Next;
                }
            }

            if (_completed)
            {
                _ = channel.Writer.TryComplete();
                return new LogSubscription(this, channel);
            }

            _subscribers.Add(channel);
        }

        return new LogSubscription(this, channel);
    }

    internal void Unsubscribe(Channel<LogLine> channel)
    {
        lock (_gate)
        {
            _subscribers.Remove(channel);
        }
    }

    public void Complete()
    {
        lock (_gate)
        {
            if (_completed)
            {
                return;
            }

            _completed = true;
            foreach (var subscriber in _subscribers)
            {
                _ = subscriber.Writer.TryComplete();
            }

            _subscribers.Clear();
        }
    }

    public IAsyncEnumerable<LogLine> StreamAsync(CancellationToken cancellationToken = default) =>
        StreamAsync(int.MaxValue, cancellationToken);

    public async IAsyncEnumerable<LogLine> StreamAsync(int historyLines, [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        // Fast path for completed broadcasters: replay snapshot without a channel.
        bool completed;
        lock (_gate)
        {
            completed = _completed;
        }

        if (completed)
        {
            var snapshot = Snapshot(historyLines);
            foreach (var line in snapshot)
            {
                cancellationToken.ThrowIfCancellationRequested();
                yield return line;
            }

            yield break;
        }

        using var subscription = Subscribe(historyLines);
        await foreach (var line in subscription.Reader.ReadAllAsync(cancellationToken).ConfigureAwait(false))
        {
            yield return line;
        }
    }
}

/// <summary>Removes its channel from the broadcaster when disposed.</summary>
internal sealed class LogSubscription : IDisposable
{
    private readonly LogBroadcaster _owner;
    private readonly Channel<LogLine> _channel;
    private int _disposed;

    public LogSubscription(LogBroadcaster owner, Channel<LogLine> channel)
    {
        _owner = owner;
        _channel = channel;
    }

    public ChannelReader<LogLine> Reader => _channel.Reader;

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _owner.Unsubscribe(_channel);
        _ = _channel.Writer.TryComplete();
    }
}
