using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslcEnvironmentTests
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
        Assert.Equal(expected, WslcEnvironment.IsContinuousIntegrationVariable(name, value));
    }
}
