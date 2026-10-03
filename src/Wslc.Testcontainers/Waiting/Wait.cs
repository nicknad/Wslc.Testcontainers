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

    /// <summary>Waits until an HTTP GET against the given Linux port succeeds (2xx-4xx; 5xx retries).</summary>
    /// <param name="pathAndQuery">Absolute path with optional query (e.g. <c>/health?ready=1</c>). Not a full URL.</param>
    /// <param name="port">Linux container port to probe via its mapped host port.</param>
    public IWaitStrategy UntilHttpRequestIsSucceeded(string pathAndQuery, int port) =>
        Configure(new HttpWaitStrategy(RequirePath(pathAndQuery, nameof(pathAndQuery)), ValidatePort(port)));

    /// <summary>Waits until an HTTP GET against Linux port 80 succeeds.</summary>
    /// <param name="pathAndQuery">Absolute path with optional query (e.g. <c>/health</c>). Not a full URL.</param>
    public IWaitStrategy UntilHttpRequestIsSucceeded(string pathAndQuery) =>
        UntilHttpRequestIsSucceeded(pathAndQuery, 80);

    /// <summary>Waits until a process with the given name is running.</summary>
    public IWaitStrategy UntilProcessIsRunning(string processName) =>
        Configure(new ProcessRunningWaitStrategy(RequireText(processName, nameof(processName))));

    /// <summary>Waits until no process with the given name is running.</summary>
    public IWaitStrategy UntilProcessExits(string processName) =>
        Configure(new ProcessExitsWaitStrategy(RequireText(processName, nameof(processName))));

    /// <summary>
    /// Waits until a message appears in captured stdout/stderr (ordinal substring, case-sensitive).
    /// WSLC diagnostics (<c>LogSource.System</c>) are ignored; regex is not supported.
    /// </summary>
    public IWaitStrategy UntilMessageIsLogged(string message) =>
        UntilMessageIsLogged(message, occurrences: 1);

    /// <summary>
    /// Waits until a message appears at least <paramref name="occurrences"/> times in captured
    /// stdout/stderr. Use for servers whose entrypoint logs readiness once from a temporary
    /// initialization process before restarting, e.g. the Postgres image.
    /// </summary>
    /// <param name="message">Ordinal, case-sensitive substring to count.</param>
    /// <param name="occurrences">Minimum number of occurrences required (&gt;= 1).</param>
    public IWaitStrategy UntilMessageIsLogged(string message, int occurrences)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(occurrences, 1);
        return Configure(new LogMessageWaitStrategy(RequireText(message, nameof(message)), occurrences));
    }

    /// <summary>Waits until an absolute Linux path exists inside the environment (e.g. <c>/tmp/ready</c>).</summary>
    public IWaitStrategy UntilFileExists(string path) =>
        Configure(new FileExistsWaitStrategy(RequireContainerPath(path, nameof(path))));

    /// <summary>
    /// Waits until a custom condition returns <c>true</c>. The delegate receives the environment
    /// and the linked timeout/cancellation token; a thrown exception propagates out of
    /// <c>WaitAsync</c> instead of being retried. Use <see cref="WithTimeout"/> /
    /// <see cref="WithRetryInterval"/> to bound and pace the polls.
    /// </summary>
    /// <param name="name">Human-readable condition name used in diagnostics.</param>
    /// <param name="condition">Condition to poll; return <c>true</c> when satisfied.</param>
    public IWaitStrategy Until(string name, Func<IWaitTarget, CancellationToken, Task<bool>> condition)
    {
        ArgumentNullException.ThrowIfNull(condition);
        return Configure(new DelegateWaitStrategy(RequireText(name, nameof(name)), condition));
    }

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

    private static string RequirePath(string value, string parameterName)
    {
        RequireText(value, parameterName);
        if (value.Contains("://", StringComparison.Ordinal) || value.StartsWith("http:", StringComparison.OrdinalIgnoreCase) || value.StartsWith("https:", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException($"HTTP wait path '{value}' must be a path-and-query (e.g. /health), not a full URL.", parameterName);
        }

        if (!value.StartsWith('/'))
        {
            throw new ArgumentException($"HTTP wait path '{value}' must start with '/'.", parameterName);
        }

        return value;
    }

    private static string RequireContainerPath(string value, string parameterName)
    {
        RequireText(value, parameterName);
        if (!value.StartsWith('/'))
        {
            throw new ArgumentException($"Container path '{value}' must be an absolute Linux path starting with '/'.", parameterName);
        }

        return value;
    }
}
