namespace Wslc.Testcontainers.Waiting;

/// <summary>Entry point for declaratively describing readiness conditions.</summary>
public static class Wait
{
    /// <summary>Starts configuring wait strategies for a WSLC environment.</summary>
    public static WslWaitBuilder ForWsl() => new();
}

/// <summary>Fluent factory for the built-in wait strategies.</summary>
public sealed class WslWaitBuilder
{
    private TimeSpan? _timeout;
    private TimeSpan? _retryInterval;

    internal WslWaitBuilder()
    {
    }

    /// <summary>Sets the timeout applied to every strategy created from this builder.</summary>
    public WslWaitBuilder WithTimeout(TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        _timeout = timeout;
        return this;
    }

    /// <summary>Sets the retry interval applied to every strategy created from this builder.</summary>
    public WslWaitBuilder WithRetryInterval(TimeSpan retryInterval)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(retryInterval, TimeSpan.Zero);
        _retryInterval = retryInterval;
        return this;
    }

    /// <summary>Waits until a Linux TCP port accepts connections.</summary>
    public IWaitStrategy UntilTcpPortIsAvailable(int port) =>
        Configure(new TcpPortWaitStrategy(ValidatePort(port)));

    /// <summary>Waits until an HTTP GET against the given Linux port succeeds.</summary>
    public IWaitStrategy UntilHttpRequestIsSucceeded(string path, int port) =>
        Configure(new HttpWaitStrategy(RequireText(path, nameof(path)), ValidatePort(port)));

    /// <summary>Waits until an HTTP GET against Linux port 80 succeeds.</summary>
    public IWaitStrategy UntilHttpRequestIsSucceeded(string path) =>
        UntilHttpRequestIsSucceeded(path, 80);

    /// <summary>Waits until a process with the given name is running.</summary>
    public IWaitStrategy UntilProcessIsRunning(string processName) =>
        Configure(new ProcessRunningWaitStrategy(RequireText(processName, nameof(processName))));

    /// <summary>Waits until no process with the given name is running.</summary>
    public IWaitStrategy UntilProcessExits(string processName) =>
        Configure(new ProcessExitsWaitStrategy(RequireText(processName, nameof(processName))));

    /// <summary>Waits until a message appears in the captured output.</summary>
    public IWaitStrategy UntilMessageIsLogged(string message) =>
        Configure(new LogMessageWaitStrategy(RequireText(message, nameof(message))));

    /// <summary>Waits until a path exists inside the environment.</summary>
    public IWaitStrategy UntilFileExists(string path) =>
        Configure(new FileExistsWaitStrategy(RequireText(path, nameof(path))));

    private IWaitStrategy Configure(IWaitStrategy strategy)
    {
        if (_timeout is { } timeout)
        {
            strategy = strategy.WithTimeout(timeout);
        }

        if (_retryInterval is { } retryInterval)
        {
            strategy = strategy.WithRetryInterval(retryInterval);
        }

        return strategy;
    }

    private static int ValidatePort(int port)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(port, 1);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);
        return port;
    }

    private static string RequireText(string value, string parameterName) =>
        string.IsNullOrWhiteSpace(value)
            ? throw new ArgumentException("Value must not be empty.", parameterName)
            : value;
}
