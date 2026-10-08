using System.Net;
using System.Net.Sockets;
using Wslc.Testcontainers.Tests.Support;
using Wslc.Testcontainers.Waiting;
using Xunit;

namespace Wslc.Testcontainers.Tests.Waiting;

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
            .UntilTcpPortIsOpen(5432);

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
            .UntilTcpPortIsOpen(5432);

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
            .UntilTcpPortIsOpen(5432);

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
    public async Task Http_wait_does_not_follow_redirects()
    {
        using var destination = new TinyHttpServer();
        using var redirect = new TinyHttpServer(statusCode: 302, location: $"http://127.0.0.1:{destination.Port}/final");
        var target = new FakeWaitTarget { ConnectEndpoint = new IPEndPoint(IPAddress.Loopback, redirect.Port) };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilHttpRequestSucceeds("/start", 8080);

        // 302 is below 500, so the probe succeeds without chasing Location to another host.
        await strategy.WaitAsync(target, CancellationToken.None);

        Assert.True(redirect.RequestCount >= 1);
        Assert.Equal(0, destination.RequestCount);
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
    public async Task Log_message_strategy_can_require_multiple_occurrences()
    {
        var target = new FakeWaitTarget();
        target.Logs.Add(new LogLine(LogSource.Stdout, "ready", DateTimeOffset.UtcNow));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(250))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilMessageIsLogged("ready", occurrences: 2);

        // One occurrence is not enough; the Postgres entrypoint pattern needs the second.
        await Assert.ThrowsAsync<WslReadinessException>(
            () => strategy.WaitAsync(target, CancellationToken.None));

        target.Logs.Add(new LogLine(LogSource.Stdout, "ready", DateTimeOffset.UtcNow));
        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Log_message_occurrences_are_counted_within_a_single_line()
    {
        var target = new FakeWaitTarget();
        target.Logs.Add(new LogLine(LogSource.Stdout, "ready ready", DateTimeOffset.UtcNow));

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(2))
            .WithRetryInterval(TimeSpan.FromMilliseconds(10))
            .UntilMessageIsLogged("ready", occurrences: 2);

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
            .UntilTcpPortIsOpen(8080)
            .And(Wait.ForWsl().UntilProcessIsRunning("nginx"))
            .And(Wait.ForWsl().UntilMessageIsLogged("ready"));

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Http_strategy_succeeds_for_non_server_errors()
    {
        using var server = new TinyHttpServer(statusCode: 200);
        var target = new FakeWaitTarget { ConnectEndpoint = new IPEndPoint(IPAddress.Loopback, server.Port) };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(5))
            .WithRetryInterval(TimeSpan.FromMilliseconds(50))
            .UntilHttpRequestSucceeds("/health", 8080);

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Http_strategy_probes_the_connect_endpoint()
    {
        using var server = new TinyHttpServer(statusCode: 200);
        // Only the mapping's effective endpoint (bind address + assigned port) leads to the server.
        var target = new FakeWaitTarget
        {
            ConnectEndpoint = new IPEndPoint(IPAddress.Loopback, server.Port),
        };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(5))
            .WithRetryInterval(TimeSpan.FromMilliseconds(50))
            .UntilHttpRequestSucceeds("/health", 8080);

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    [Fact]
    public async Task Http_strategy_brackets_ipv6_connect_endpoints()
    {
        var server = TryCreateIpv6Server();
        if (server is null)
        {
            Assert.Skip("IPv6 loopback is not available in this environment.");
        }

        using var listener = server;

        // The probe URI is built from IPEndPoint.ToString(), which brackets the IPv6 literal.
        // Without the brackets the authority would not parse and the wait could never succeed.
        var target = new FakeWaitTarget
        {
            ConnectEndpoint = new IPEndPoint(IPAddress.IPv6Loopback, server!.Port),
        };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(5))
            .WithRetryInterval(TimeSpan.FromMilliseconds(50))
            .UntilHttpRequestSucceeds("/health", 8080);

        await strategy.WaitAsync(target, CancellationToken.None);
    }

    private static TinyHttpServer? TryCreateIpv6Server()
    {
        try
        {
            return new TinyHttpServer(statusCode: 200, address: IPAddress.IPv6Loopback);
        }
        catch (SocketException)
        {
            return null;
        }
    }

    [Fact]
    public async Task Http_strategy_times_out_for_server_errors()
    {
        using var server = new TinyHttpServer(statusCode: 500);
        var target = new FakeWaitTarget { ConnectEndpoint = new IPEndPoint(IPAddress.Loopback, server.Port) };

        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(300))
            .WithRetryInterval(TimeSpan.FromMilliseconds(25))
            .UntilHttpRequestSucceeds("/health", 8080);

        await Assert.ThrowsAsync<WslReadinessException>(
            () => strategy.WaitAsync(target, CancellationToken.None));
    }

    [Fact]
    public async Task Custom_until_condition_polls_until_satisfied()
    {
        var attempts = 0;
        // Generous budget: the condition needs three polls, and thread-pool/CI scheduling can
        // delay the 10ms retry continuations well beyond that on loaded runners (arm64 flakes).
        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromSeconds(15))
            .WithRetryInterval(TimeSpan.FromMilliseconds(50))
            .Until("custom flag", (_, _) => Task.FromResult(Interlocked.Increment(ref attempts) >= 3));

        await strategy.WaitAsync(new FakeWaitTarget(), CancellationToken.None);

        Assert.True(attempts >= 3);
        Assert.Equal("custom flag", strategy.Name);
    }

    [Fact]
    public void Composite_keeps_the_left_operand_timeout_and_retry_interval()
    {
        var strategy = Wait.ForWsl()
            .WithTimeout(TimeSpan.FromMilliseconds(150))
            .WithRetryInterval(TimeSpan.FromMilliseconds(25))
            .UntilTcpPortIsOpen(8080)
            .And(Wait.ForWsl().WithTimeout(TimeSpan.FromSeconds(30)).UntilProcessIsRunning("nginx"));

        Assert.Equal(TimeSpan.FromMilliseconds(150), strategy.Timeout);
        Assert.Equal(TimeSpan.FromMilliseconds(25), strategy.RetryInterval);
    }

    [Fact]
    public void Custom_until_condition_validates_arguments()
    {
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().Until(" ", (_, _) => Task.FromResult(true)));
        Assert.Throws<ArgumentNullException>(() => Wait.ForWsl().Until("condition", null!));
    }

    [Fact]
    public void Invalid_configuration_is_rejected()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().WithTimeout(TimeSpan.Zero));
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().WithRetryInterval(TimeSpan.Zero));
        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().UntilTcpPortIsOpen(0));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists(" "));

        // HTTP waits take a path-and-query, never a full URL or a relative path.
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestSucceeds("http://localhost/health", 8080));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestSucceeds("health", 8080));

        // Raw request lines must not carry spaces or CR/LF that would inject headers.
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestSucceeds("/health HTTP/1.1\r\nX-Evil: 1", 8080));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestSucceeds("/he alth", 8080));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilHttpRequestSucceeds("/health\tx", 8080));

        // Container paths cannot escape via '..' or target kernel pseudo-filesystems.
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists("/tmp/../etc/passwd"));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists("/./proc/self/environ"));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists("//sys/kernel"));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists("/dev/sda"));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilFileExists("/tmp/bad\u0001name"));

        Assert.Throws<ArgumentOutOfRangeException>(() => Wait.ForWsl().UntilMessageIsLogged("ready", 0));
        Assert.Throws<ArgumentException>(() => Wait.ForWsl().UntilMessageIsLogged(" ", 2));
    }
}
