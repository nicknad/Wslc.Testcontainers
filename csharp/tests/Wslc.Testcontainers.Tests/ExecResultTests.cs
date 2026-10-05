using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class ExecResultTests
{
    [Fact]
    public void ToString_is_bounded_and_hides_the_captured_payload()
    {
        var result = new ExecResult(7, new string('o', 20000), new string('e', 15000));

        var text = result.ToString();

        Assert.Contains("ExitCode = 7", text, StringComparison.Ordinal);
        Assert.Contains("20000", text, StringComparison.Ordinal);
        Assert.Contains("15000", text, StringComparison.Ordinal);
        Assert.DoesNotContain("oooo", text, StringComparison.Ordinal);
        Assert.DoesNotContain("eeee", text, StringComparison.Ordinal);
        Assert.True(text.Length < 200, $"Summary should stay short but was {text.Length} chars.");
    }

    [Fact]
    public void Equality_and_behavior_are_unchanged()
    {
        var first = new ExecResult(0, "out", "err");
        var second = new ExecResult(0, "out", "err");

        Assert.Equal(first, second);
        Assert.True(first.Succeeded);
        Assert.Same(first, first.EnsureSuccess());
    }
}
