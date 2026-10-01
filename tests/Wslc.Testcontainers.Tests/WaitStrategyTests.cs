using Wslc.Testcontainers.Tests.Support;
using Wslc.Testcontainers.Waiting;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WaitStrategyTests
{
    [Fact]
    public async Task Tcp_port_strategy_polls_until_available()
    {
        var attempts = 0;
        var target = new FakeWaitTarget
        {
            PortHandler = (_, _) => Task.FromResult(Interlocked.Increment(ref attempts) >= 3),
        };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(5))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilTcpPortIsAvailable(5432);

        await strategy.WaitAsync(target, CancellationToken.None);

        Assert.True(attempts >= 3);
    }

    [Fact]
    public async Task Tcp_port_strategy_times_out_with_diagnostics()
    {
        var target = new FakeWaitTarget
        {
            PortHandler = (_, _) => Task.FromResult(false),
        };
        target.Logs.Add(LogLine.Diagnostic("database starting"));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(120))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilTcpPortIsAvailable(5432);

        var exception = await Assert.ThrowsAsync<WslReadinessException>(
            () => strategy.WaitAsync(target, CancellationToken.None));

        Assert.Contains("TCP port 5432", exception.ExpectedCondition);
        Assert.Equal(TimeSpan.FromMilliseconds(120), exception.Timeout);
        Assert.Contains(exception.Logs, line => line.Text == "database starting");
    }

    [Fact]
    public async Task Strategy_honors_cancellation()
    {
        var target = new FakeWaitTarget
        {
            PortHandler = (_, _) => Task.FromResult(false),
        };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(30))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilTcpPortIsAvailable(5432);

        using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(100));

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => strategy.WaitAsync(target, cancellation.Token));
    }

    [Fact]
    public async Task Process_running_and_exit_strategies_use_the_target()
    {
        var running = true;
        var target = new FakeWaitTarget
        {
            ProcessHandler = (_, _) => Task.FromResult(running),
        };

        var waitUntilRunning = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilProcessIsRunning("postgres");

        await waitUntilRunning.WaitAsync(target, CancellationToken.None);

        running = false;
        var waitUntilExited = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilProcessExits("postgres");

        await waitUntilExited.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task File_exists_strategy_executes_test_command()
    {
        var target = new FakeWaitTarget
        {
            ExecHandler = (_, arguments, _) =>
                Task.FromResult(new ExecResult(arguments.Contains("/tmp/ready") ? 0 : 1, string.Empty, string.Empty)),
        };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilFileExists("/tmp/ready");

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Log_message_strategy_matches_captured_logs()
    {
        var target = new FakeWaitTarget();
        target.Logs.Add(new LogLine(LogSource.Stdout, "database system is ready to accept connections", DateTimeOffset.UtcNow));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilMessageIsLogged("database system is ready");

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Log_message_strategy_ignores_wslc_diagnostics()
    {
        var target = new FakeWaitTarget();
        target.Logs.Add(LogLine.Diagnostic("never-appears"));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(200))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilMessageIsLogged("never-appears");

        await Assert.ThrowsAsync<WslReadinessException>(
            () => strategy.WaitAsync(target, CancellationToken.None));
    }

    [Fact]
    public async Task Composite_strategies_require_all_conditions()
    {
        var target = new FakeWaitTarget
        {
            PortHandler = (_, _) => Task.FromResult(true),
            ProcessHandler = (_, _) => Task.FromResult(true),
        };
        target.Logs.Add(new LogLine(LogSource.Stdout, "ready", DateTimeOffset.UtcNow));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilTcpPortIsAvailable(8080)
            .And(Wait.ForWsl().UntilProcessIsRunning("nginx"))
            .And(Wait.ForWsl().UntilMessageIsLogged("ready"));

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Http_strategy_succeeds_for_non_server_errors()
    {
        using var server = new TinyHttpServer(statusCode: 200);
        var target = new FakeWaitTarget { MappedPort = server.Port };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(5))
            .WithRetryInterval(TimeSpan.FromMilliseconds(50))
            .UntilHttpRequestIsSucceeded("/health", 8080);

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Http_strategy_times_out_for_server_errors()
    {
        using var server = new TinyHttpServer(statusCode: 500);
        var target = new FakeWaitTarget { MappedPort = server.Port };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(300))
            .WithRetryInterval(TimeSpan.FromMilliseconds(25))
            .UntilHttpRequestIsSucceeded("/health", 8080);

        await Assert.ThrowsAsync<WslReadinessException>(
            () => strategy.WaitAsync(target, CancellationToken.None));
    }

    [Fact]
    public void Invalid_configuration_is_rejected()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().WithTimeout(TimeSpan.Zero));
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().WithRetryInterval(TimeSpan.Zero));
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().UntilTcpPortIsAvailable(0));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists(" "));

        // HTTP waits take a path-and-query, never a full URL or a relative path.
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestIsSucceeded("http://localhost/health", 8080));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestIsSucceeded("health", 8080));
    }
}
