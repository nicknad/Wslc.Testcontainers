using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslContainerLifecycleTests
{
    [Fact]
    public async Task Dispose_completes_logs_and_is_idempotent()
    {
        var container = CreateContainer();

        await container.DisposeAsync();
        await container.DisposeAsync();

        Assert.Empty(await DrainAsync(container));
    }

    [Fact]
    public async Task Dispose_after_process_exit_cleanup_still_completes_logs()
    {
        var container = CreateContainer();

        container.CleanupSynchronously();
        await container.DisposeAsync();

        Assert.Empty(await DrainAsync(container));
    }

    [Theory]
    [InlineData(typeof(IOException), true)]
    [InlineData(typeof(UnauthorizedAccessException), true)]
    [InlineData(typeof(OperationCanceledException), false)]
    [InlineData(typeof(InvalidOperationException), false)]
    public void Reuse_lock_retries_only_transient_file_errors(Type exceptionType, bool expected)
    {
        var exception = (Exception)Activator.CreateInstance(exceptionType)!;

        Assert.Equal(expected, WslContainer.IsRetryableReuseLockError(exception));
    }

    private static WslContainer CreateContainer()
    {
        var store = new WslInstanceStore(
            Path.Combine(Path.GetTempPath(), "wslc-tests", Guid.NewGuid().ToString("N")),
            "lifecycle-session");
        return new WslContainer(new WslContainerConfiguration(), store);
    }

    private static async Task<List<LogLine>> DrainAsync(WslContainer container)
    {
        var lines = new List<LogLine>();
        await foreach (var line in container.SubscribeLogs(TestContext.Current.CancellationToken))
        {
            lines.Add(line);
        }

        return lines;
    }
}
