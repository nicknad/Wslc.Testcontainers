namespace Wslc.Testcontainers.Waiting;

/// <summary>A readiness condition evaluated after the environment has been provisioned.</summary>
public interface IWaitStrategy
{
    /// <summary>Gets a human-readable description of the awaited condition.</summary>
    string Name { get; }

    /// <summary>Gets the maximum time to wait.</summary>
    TimeSpan Timeout { get; }

    /// <summary>Gets the interval between checks.</summary>
    TimeSpan RetryInterval { get; }

    /// <summary>Returns a copy of this strategy with a different timeout (must be positive).</summary>
    IWaitStrategy WithTimeout(TimeSpan timeout);

    /// <summary>Returns a copy of this strategy with a different retry interval (must be positive).</summary>
    IWaitStrategy WithRetryInterval(TimeSpan retryInterval);

    /// <summary>
    /// Combines this strategy with another one; both must be satisfied sequentially.
    /// Nested composites are flattened; the composite timeout bounds the whole sequence.
    /// </summary>
    IWaitStrategy And(IWaitStrategy other);

    /// <summary>Waits until the condition is satisfied or the timeout elapses.</summary>
    Task WaitAsync(IWaitTarget target, CancellationToken cancellationToken);
}
