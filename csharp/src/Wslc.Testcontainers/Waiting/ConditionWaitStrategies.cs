namespace Wslc.Testcontainers.Waiting;

/// <summary>Waits until a Linux process with the given name is running.</summary>
internal sealed record ProcessRunningWaitStrategy(string ProcessName) : PollingWaitStrategyBase
{
    public override string Name => $"process '{ProcessName}' to be running";

    protected override Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken) =>
        target.IsProcessRunningAsync(ProcessName, cancellationToken);
}

/// <summary>Waits until no Linux process with the given name is running.</summary>
internal sealed record ProcessExitsWaitStrategy(string ProcessName) : PollingWaitStrategyBase
{
    public override string Name => $"process '{ProcessName}' to have exited";

    protected override async Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        var running = await target.IsProcessRunningAsync(ProcessName, cancellationToken).ConfigureAwait(false);
        return !running;
    }
}

/// <summary>Waits until a path exists inside the environment.</summary>
internal sealed record FileExistsWaitStrategy(string Path) : PollingWaitStrategyBase
{
    public override string Name => $"path '{Path}' to exist";

    protected override async Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        var result = await target.ExecAsync("test", new[] { "-e", Path }, cancellationToken).ConfigureAwait(false);
        return result.ExitCode == 0;
    }
}

/// <summary>Waits until a message appears in the captured logs a given number of times.</summary>
internal sealed record LogMessageWaitStrategy(string Message, int Occurrences = 1) : PollingWaitStrategyBase
{
    public override string Name => Occurrences <= 1
        ? $"log message '{Message}' to be logged"
        : $"log message '{Message}' to be logged {Occurrences} times";

    protected override Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        var logs = target.GetRecentLogs();
        var seen = 0;
        for (var i = 0; i < logs.Count; i++)
        {
            var line = logs[i];
            if (line.Source == LogSource.System)
            {
                continue;
            }

            var index = line.Text.IndexOf(Message, StringComparison.Ordinal);
            while (index >= 0)
            {
                seen++;
                if (seen >= Occurrences)
                {
                    return Task.FromResult(true);
                }

                index = line.Text.IndexOf(Message, index + Message.Length, StringComparison.Ordinal);
            }
        }

        return Task.FromResult(false);
    }
}

/// <summary>Waits until a caller-provided condition returns true.</summary>
internal sealed record DelegateWaitStrategy(string ConditionName, Func<IWaitTarget, CancellationToken, Task<bool>> Condition) : PollingWaitStrategyBase
{
    public override string Name => ConditionName;

    protected override Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken) =>
        Condition(target, cancellationToken);
}
