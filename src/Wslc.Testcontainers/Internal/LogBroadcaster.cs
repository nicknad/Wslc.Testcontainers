using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Wslc.Testcontainers.Internal;

/// <summary>Thread-safe fan-out log buffer with bounded history and bounded subscribers.</summary>
/// <remarks>
/// History is capped at 10k lines. Each subscriber channel is bounded (1k, DropOldest) so an
/// abandoned <c>LogsAsync</c> enumeration cannot grow memory without bound; still, callers must
/// cancel/dispose log streams promptly (see <c>LogDumper</c>).
/// </remarks>
internal sealed class LogBroadcaster
{
    private const int MaxHistory = 10_000;
    private const int MaxSubscriberBuffered = 1_000;
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

    public IReadOnlyList<LogLine> Snapshot()
    {
        lock (_gate)
        {
            return _history.ToArray();
        }
    }

    public LogSubscription Subscribe()
    {
        var channel = Channel.CreateBounded<LogLine>(new BoundedChannelOptions(MaxSubscriberBuffered)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.DropOldest,
        });

        lock (_gate)
        {
            foreach (var line in _history)
            {
                _ = channel.Writer.TryWrite(line);
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

    public async IAsyncEnumerable<LogLine> StreamAsync([EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        // Fast path for completed broadcasters: replay snapshot without a channel.
        bool completed;
        lock (_gate)
        {
            completed = _completed;
        }

        if (completed)
        {
            foreach (var line in Snapshot())
            {
                cancellationToken.ThrowIfCancellationRequested();
                yield return line;
            }

            yield break;
        }

        using var subscription = Subscribe();
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
