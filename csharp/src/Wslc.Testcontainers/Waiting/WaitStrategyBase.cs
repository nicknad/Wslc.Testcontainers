using System.Diagnostics;
using Wslc.Testcontainers.Internal;

namespace Wslc.Testcontainers.Waiting;

/// <summary>Shared timeout/retry configuration for declarative wait strategies.</summary>
internal abstract record WaitStrategyBase : IWaitStrategy
{
    internal static readonly TimeSpan DefaultRetryInterval = TimeSpan.FromMilliseconds(250);

    private const int MaxLogLines = 50;

    public abstract string Name { get; }

    public TimeSpan Timeout { get; init; } = WslEnvironment.DefaultWaitTimeout;

    public TimeSpan RetryInterval { get; init; } = DefaultRetryInterval;

    public IWaitStrategy WithTimeout(TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        return this with { Timeout = timeout };
    }

    public IWaitStrategy WithRetryInterval(TimeSpan retryInterval)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(retryInterval, TimeSpan.Zero);
        return this with { RetryInterval = retryInterval };
    }

    public IWaitStrategy And(IWaitStrategy other)
    {
        ArgumentNullException.ThrowIfNull(other);

        // Composites flatten their children, so the top-level Build count cannot see nested
        // strategies; enforce the combined cap here instead.
        var combined = new List<IWaitStrategy>(CountStrategies(this) + CountStrategies(other));
        AddFlattened(combined, this);
        AddFlattened(combined, other);

        BuilderLimits.RequireWaitStrategyCount(combined.Count);
        return new CompositeWaitStrategy(combined) with { Timeout = Timeout, RetryInterval = RetryInterval };
    }

    private static void AddFlattened(List<IWaitStrategy> combined, IWaitStrategy strategy)
    {
        if (strategy is CompositeWaitStrategy composite)
        {
            combined.AddRange(composite.Strategies);
        }
        else
        {
            combined.Add(strategy);
        }
    }

    private static int CountStrategies(IWaitStrategy strategy) =>
        strategy is CompositeWaitStrategy composite ? composite.Strategies.Count : 1;

    public abstract Task WaitAsync(IWaitTarget target, CancellationToken cancellationToken);

    protected WslReadinessException CreateTimeout(IWaitTarget target, TimeSpan elapsed)
    {
        var message = $"Timed out after {elapsed.TotalSeconds:0.###}s waiting for {Name} on '{target.Name}'.";

        return new WslReadinessException(message, Name, Timeout, LogHelpers.TakeLast(target.GetRecentLogs(), MaxLogLines));
    }
}

/// <summary>A wait strategy that polls a condition until it holds or the timeout elapses.</summary>
internal abstract record PollingWaitStrategyBase : WaitStrategyBase
{
    public override async Task WaitAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(target);
        // Per ADR bounded loops we don't allow while(true): poll with a fixed iteration cap
        // so a misconfigured Timeout/RetryInterval still terminates. 100k covers 60s @ 250ms.
        const int MaxIterations = 100_000;
        using var timeoutSource = new CancellationTokenSource(Timeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutSource.Token);
        var stopwatch = Stopwatch.StartNew();

        for (var attempt = 0; attempt < MaxIterations; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            bool satisfied;
            try
            {
                satisfied = await CheckAsync(target, linked.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw CreateTimeout(target, stopwatch.Elapsed);
            }

            if (satisfied)
            {
                return;
            }

            if (stopwatch.Elapsed >= Timeout)
            {
                throw CreateTimeout(target, stopwatch.Elapsed);
            }

            try
            {
                await Task.Delay(RetryInterval, linked.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw CreateTimeout(target, stopwatch.Elapsed);
            }
        }

        throw CreateTimeout(target, stopwatch.Elapsed);
    }

    protected abstract Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken);
}
