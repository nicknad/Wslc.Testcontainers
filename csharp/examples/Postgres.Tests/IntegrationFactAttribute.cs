using System.Runtime.CompilerServices;
using Xunit;

namespace Customers.Tests;

/// <summary>Runs a test only when WSL integration tests have been explicitly enabled.</summary>
public sealed class IntegrationFactAttribute : FactAttribute
{
    public const string EnvironmentVariable = "WSLC_RUN_INTEGRATION";

    // Bound the real-runtime test so a wedged WSL session fails the test (the xUnit v3
    // timeout signals TestContext.Current.CancellationToken) instead of hanging the test host.
    private const int DefaultTimeoutMilliseconds = 10 * 60 * 1000;

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
