namespace Wslc.Testcontainers.Waiting;

/// <summary>Requires several strategies to be satisfied. They are evaluated sequentially within the composite timeout.</summary>
internal sealed record CompositeWaitStrategy(IReadOnlyList<IWaitStrategy> Strategies) : WaitStrategyBase
{
    public override string Name =>
        string.Join(" and ", Strategies.Select(strategy => strategy.Name));

    public override async Task WaitAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(target);
        // The composite Timeout bounds the whole sequence; child strategies keep their own
        // timeouts for diagnostics, but the composite CTS guarantees we never exceed it.
        using var timeoutSource = new CancellationTokenSource(Timeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutSource.Token);
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            foreach (var strategy in Strategies)
            {
                await strategy.WaitAsync(target, linked.Token).ConfigureAwait(false);
                cancellationToken.ThrowIfCancellationRequested();
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw CreateTimeout(target, stopwatch.Elapsed);
        }
    }
}
