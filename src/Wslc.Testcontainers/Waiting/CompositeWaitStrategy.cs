namespace Wslc.Testcontainers.Waiting;

/// <summary>Requires several strategies to be satisfied. They are evaluated sequentially.</summary>
internal sealed record CompositeWaitStrategy(IReadOnlyList<IWaitStrategy> Strategies) : WaitStrategyBase
{
    public override string Name =>
        string.Join(" and ", Strategies.Select(strategy => strategy.Name));

    public override async Task WaitAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        foreach (var strategy in Strategies)
        {
            await strategy.WaitAsync(target, cancellationToken).ConfigureAwait(false);
        }
    }
}
