using System.Runtime.InteropServices;
using Xunit;

namespace Wslc.Testcontainers.Tests;

/// <summary>
/// Cross-platform guard tests. This is the only suite that runs on non-Windows
/// CI (ubuntu-latest) to prove fast, actionable failure outside Windows.
/// </summary>
public sealed class WslPlatformTests
{
    [Fact]
    public void IsSupported_matches_os_and_architecture()
    {
        var expected = OperatingSystem.IsWindowsVersionAtLeast(10, 0, 19041) &&
            RuntimeInformation.ProcessArchitecture is Architecture.X64 or Architecture.Arm64;

        Assert.Equal(expected, WslPlatform.IsSupported);
    }

    [Fact]
    public void ThrowIfUnsupported_matches_IsSupported()
    {
        if (WslPlatform.IsSupported)
        {
            WslPlatform.ThrowIfUnsupported();
        }
        else
        {
            var exception = Assert.Throws<PlatformNotSupportedException>(WslPlatform.ThrowIfUnsupported);
            Assert.Contains("Windows", exception.Message, StringComparison.OrdinalIgnoreCase);
            Assert.Contains("WSL", exception.Message, StringComparison.OrdinalIgnoreCase);
        }
    }

    [Fact]
    public void DescribeCurrent_mentions_architecture()
    {
        var description = WslPlatform.DescribeCurrent();

        Assert.False(string.IsNullOrWhiteSpace(description));
        Assert.Contains(RuntimeInformation.ProcessArchitecture.ToString(), description, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData(2, 9, 3, true)]
    [InlineData(2, 9, 2, false)]
    [InlineData(2, 8, 99, false)]
    [InlineData(2, 10, 0, true)]
    [InlineData(3, 0, 0, true)]
    [InlineData(3, 0, 1, true)]
    [InlineData(1, 99, 99, false)]
    public void Wsl_version_gate_enforces_minimum(uint major, uint minor, uint patch, bool expected)
    {
        Assert.Equal(expected, WslPlatform.IsWslVersionSupported(major, minor, patch));
        Assert.Equal(expected, WslPlatform.IsWslVersionSupported(new Version((int)major, (int)minor, (int)patch)));
    }

    [Fact]
    public async Task Build_fails_fast_on_unsupported_platform()
    {
        var builder = new WslContainerBuilder().FromImage("docker.io/library/alpine:latest");

        if (!WslPlatform.IsSupported)
        {
            Assert.Throws<PlatformNotSupportedException>(() => builder.Build());
        }
        else
        {
            await using var container = builder.Build();
            Assert.NotNull(container);
        }
    }
}
