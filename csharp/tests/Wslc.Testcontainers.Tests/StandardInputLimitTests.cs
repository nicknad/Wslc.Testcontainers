using Wslc.Testcontainers.Provisioning;
using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class StandardInputLimitTests
{
    [Fact]
    public void ValidateStandardInputSize_accepts_the_limit()
    {
        WslcProcessRunner.ValidateStandardInputSize(WslcProcessRunner.MaxStandardInputBytes);
    }

    [Fact]
    public void ValidateStandardInputSize_rejects_one_byte_over_the_limit()
    {
        var exception = Assert.Throws<WslProcessException>(
            () => WslcProcessRunner.ValidateStandardInputSize(WslcProcessRunner.MaxStandardInputBytes + 1));

        Assert.Contains("64 MiB", exception.Message, StringComparison.Ordinal);
        Assert.Contains($"{WslcProcessRunner.MaxStandardInputBytes + 1} bytes", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task ExecAsync_rejects_oversized_standard_input_before_container_work()
    {
        var store = new WslInstanceStore(
            Path.Combine(Path.GetTempPath(), "wslc-tests", Guid.NewGuid().ToString("N")),
            "stdin-limit-session");
        var container = new WslContainer(new WslContainerConfiguration(), store);

        // '€' encodes to three UTF-8 bytes, so this exceeds the limit with a ~22 MiB string.
        var oversized = new string('\u20AC', (int)(WslcProcessRunner.MaxStandardInputBytes / 3) + 1);

        var exception = await Assert.ThrowsAsync<WslProcessException>(
            () => container.ExecAsync(
                "cat",
                new ExecOptions { StandardInput = oversized },
                TestContext.Current.CancellationToken));

        Assert.Contains("64 MiB", exception.Message, StringComparison.Ordinal);
    }
}
