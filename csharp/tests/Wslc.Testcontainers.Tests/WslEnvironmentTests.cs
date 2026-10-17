using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslEnvironmentTests
{
    [Theory]
    [InlineData("CI", "1", true)]
    [InlineData("CI", "TRUE", true)]
    [InlineData("TF_BUILD", "True", true)]
    [InlineData("GITHUB_ACTIONS", "yes", true)]
    [InlineData("CI", "false", false)]
    [InlineData("CI", "0", false)]
    [InlineData("CI", "", false)]
    [InlineData("TF_BUILD", "not-a-bool", false)]
    [InlineData("JENKINS_URL", "https://jenkins.example.com", true)]
    [InlineData("TEAMCITY_VERSION", "2025.1", true)]
    [InlineData("JENKINS_URL", "", false)]
    [InlineData("JENKINS_URL", "   ", false)]
    [InlineData("TEAMCITY_VERSION", null, false)]
    [InlineData("SOME_OTHER", "true", false)]
    public void Continuous_integration_variables_are_classified(string name, string? value, bool expected)
    {
        Assert.Equal(expected, WslEnvironment.IsContinuousIntegrationVariable(name, value));
    }

    [Theory]
    [InlineData(true, false, true, true)]
    [InlineData(true, false, false, false)]
    [InlineData(null, true, true, true)]
    [InlineData(null, true, false, false)]
    [InlineData(null, false, true, false)]
    [InlineData(false, true, true, false)]
    public void Effective_reuse_requires_configuration_and_environment_allowance(
        bool? configuredReuse,
        bool reuseByDefault,
        bool reuseAllowed,
        bool expected)
    {
        Assert.Equal(expected, WslEnvironment.IsReuseEffective(configuredReuse, reuseByDefault, reuseAllowed));
    }

    [Fact]
    public void Relative_data_directory_becomes_absolute()
    {
        var resolved = WslEnvironment.ResolveDataDirectory("relative\\dir");

        Assert.True(Path.IsPathFullyQualified(resolved));
        Assert.Equal(Path.GetFullPath("relative\\dir"), resolved);
    }

    [Fact]
    public void Absolute_data_directory_is_accepted()
    {
        Assert.Equal(@"C:\wslc\data", WslEnvironment.ResolveDataDirectory(@"C:\wslc\data"));
    }

    [Theory]
    [InlineData(@"\\server\share")]
    [InlineData(@"\\?\C:\x")]
    [InlineData(@"\\.\x")]
    public void Unc_and_device_data_directories_are_rejected(string configured)
    {
        var exception = Assert.Throws<WslException>(() => WslEnvironment.ResolveDataDirectory(configured));

        Assert.Contains(WslEnvironment.DataDirectoryVariable, exception.Message);
    }

    [Theory]
    [InlineData("a")]
    [InlineData("abc-DEF_123")]
    [InlineData("a_b-c")]
    public void Valid_session_ids_are_accepted(string configured)
    {
        Assert.Equal(configured, WslEnvironment.ResolveSessionId(configured));
    }

    [Fact]
    public void Session_id_of_64_characters_is_accepted()
    {
        var configured = new string('a', 64);

        Assert.Equal(configured, WslEnvironment.ResolveSessionId(configured));
    }

    [Fact]
    public void Session_id_longer_than_64_characters_is_rejected()
    {
        var exception = Assert.Throws<WslException>(() => WslEnvironment.ResolveSessionId(new string('a', 65)));

        Assert.Contains(WslEnvironment.SessionIdVariable, exception.Message);
    }

    [Theory]
    [InlineData("")]
    [InlineData("has space")]
    [InlineData("has.dot")]
    [InlineData("has/slash")]
    [InlineData("has\\backslash")]
    [InlineData("has:colon")]
    public void Invalid_session_ids_are_rejected(string configured)
    {
        var exception = Assert.Throws<WslException>(() => WslEnvironment.ResolveSessionId(configured));

        Assert.Contains(WslEnvironment.SessionIdVariable, exception.Message);
    }

    [Fact]
    public void Generated_session_id_matches_the_charset_and_length_bound()
    {
        var generated = WslEnvironment.ResolveSessionId(null);

        Assert.InRange(generated.Length, 1, 64);
        Assert.All(generated, c => Assert.True(char.IsAsciiLetterOrDigit(c) || c is '-' or '_'));
    }

    [Fact]
    public void Sanitized_session_id_is_truncated_and_stays_in_the_charset()
    {
        var sanitized = WslEnvironment.SanitizeSessionId(new string('.', 100));

        Assert.Equal(new string('_', 64), sanitized);
    }

    [Theory]
    [InlineData("60", 60)]
    [InlineData("1.5", 1.5)]
    [InlineData("00:01:00", 60)]
    [InlineData("00:00:01.500", 1.5)]
    [InlineData("1.00:00:00", 86400)]
    public void ParseTimeout_accepts_seconds_and_clock_times(string value, double expectedSeconds)
    {
        Assert.Equal(TimeSpan.FromSeconds(expectedSeconds), WslEnvironment.ParseTimeout(value));
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-5")]
    [InlineData("abc")]
    [InlineData("")]
    [InlineData("   ")]
    public void ParseTimeout_rejects_non_positive_and_invalid(string value)
    {
        Assert.Null(WslEnvironment.ParseTimeout(value));
    }

    [Fact]
    public void ParseTimeout_rejects_null()
    {
        Assert.Null(WslEnvironment.ParseTimeout(null));
    }

    [Theory]
    [InlineData("1", 64u, 1u)]
    [InlineData("4", 64u, 4u)]
    [InlineData("64", 64u, 64u)]
    [InlineData("0", 64u, null)]
    [InlineData("-1", 64u, null)]
    [InlineData("65", 64u, null)]
    [InlineData("abc", 64u, null)]
    [InlineData("4.0", 64u, null)]
    [InlineData(" 4", 64u, null)]
    [InlineData("", 64u, null)]
    [InlineData(null, 64u, null)]
    public void ParseCount_accepts_only_unsigned_values_within_range(string? value, uint maximum, uint? expected)
    {
        Assert.Equal(expected, WslEnvironment.ParseCount(value, maximum));
    }
}
