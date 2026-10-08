using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests.Provisioning;

public sealed class WslNamingTests
{
    [Fact]
    public void Instance_names_are_unique_and_prefixed()
    {
        var first = WslNaming.CreateInstanceName("my-project-123");
        var second = WslNaming.CreateInstanceName("my-project-123");

        Assert.StartsWith(WslNaming.Prefix, first, StringComparison.Ordinal);
        Assert.StartsWith("wslc-my-project-123-", first, StringComparison.Ordinal);
        Assert.NotEqual(first, second);
    }

    [Fact]
    public void Reuse_names_are_derived_from_the_configuration_hash()
    {
        var hash = new string('a', 64);

        Assert.Equal("wslc-reuse-aaaaaaaaaaaa", WslNaming.CreateReuseName(hash));
    }

    [Theory]
    [InlineData("My Project/Alpha", "my-project-alpha")]
    [InlineData("___", "session")]
    [InlineData("simple", "simple")]
    public void Slugs_are_sanitized(string input, string expected)
    {
        Assert.Equal(expected, WslNaming.Slug(input));
    }

    [Fact]
    public void Slugs_are_truncated()
    {
        Assert.Equal(10, WslNaming.Slug("abcdefghijklmnop", maxLength: 10).Length);
    }

    [Theory]
    [InlineData("wslc-abc", true)]
    [InlineData("WSLC-abc", true)]
    [InlineData("Ubuntu", false)]
    [InlineData("docker-desktop", false)]
    public void Ownership_check_matches_only_managed_names(string name, bool expected)
    {
        Assert.Equal(expected, WslNaming.IsManaged(name));
    }
}
