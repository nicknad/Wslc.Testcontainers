using System.Runtime.CompilerServices;
using Xunit;

namespace Wslc.Testcontainers.Testing;

/// <summary>
/// Runs a test only when WSL integration tests have been explicitly enabled, and bounds it with a
/// timeout so a wedged session fails the test instead of hanging the test host. Shared by every
/// test project (linked in via <c>&lt;Compile Include&gt;</c>) so the gate and the timeout stay in
/// lockstep.
/// </summary>
public sealed class IntegrationFactAttribute : FactAttribute
{
    public const string EnvironmentVariable = "WSLC_RUN_INTEGRATION";

    // Generous next to every test's own waits: the largest module wait budget is Elasticsearch's
    // 5-minute WithWaitTimeout, which derives a 10.5-minute startup budget (2 * wait + 30s) plus a
    // cold image pull. The timeout signals TestContext.Current.CancellationToken, so tests must
    // observe that token (xUnit analyzer xUnit1069) for this to fire.
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
