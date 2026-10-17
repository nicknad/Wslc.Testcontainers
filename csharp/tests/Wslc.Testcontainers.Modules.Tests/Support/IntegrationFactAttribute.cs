using System.Runtime.CompilerServices;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests.Support;

/// <summary>Runs a test only when WSL integration tests have been explicitly enabled.</summary>
public sealed class IntegrationFactAttribute : FactAttribute
{
    public const string EnvironmentVariable = "WSLC_RUN_INTEGRATION";

    // Bound every real-runtime test so a wedged WSL session fails the test (the xUnit v3
    // timeout signals TestContext.Current.CancellationToken) instead of hanging the test
    // host -- and taking the CI runner down with it. The largest module wait budget is
    // Elasticsearch's 5-minute WithWaitTimeout, which derives a 10.5-minute startup budget
    // (2 * wait + 30s) plus a cold image pull; 20 minutes clears that with margin.
    private const int DefaultTimeoutMilliseconds = 20 * 60 * 1000;

    public IntegrationFactAttribute(
        [CallerFilePath] string? sourceFilePath = null,
        [CallerLineNumber] int sourceLineNumber = -1)
        : base(sourceFilePath, sourceLineNumber)
    {
        Timeout = DefaultTimeoutMilliseconds;
        if (Environment.GetEnvironmentVariable(EnvironmentVariable) != "1")
        {
            Skip = $"Set {EnvironmentVariable}=1 to run WSL container integration tests.";
        }
    }
}
