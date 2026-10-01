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

/// <summary>Waits until a message appears in the captured logs.</summary>
internal sealed record LogMessageWaitStrategy(string Message) : PollingWaitStrategyBase
{
    public override string Name => $"log message '{Message}' to be logged";

    protected override Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        var logs = target.GetRecentLogs();
        for (var i = 0; i < logs.Count; i++)
        {
            var line = logs[i];
            if (line.Source != LogSource.System &&
                line.Text.Contains(Message, StringComparison.Ordinal))
            {
                return Task.FromResult(true);
            }
        }

        return Task.FromResult(false);
    }
}
