using System.Runtime.CompilerServices;
using Xunit;

namespace Wslc.Testcontainers.Tests.Support;

/// <summary>Runs a test only when WSL integration tests have been explicitly enabled.</summary>
public sealed class IntegrationFactAttribute : FactAttribute
{
    public const string EnvironmentVariable = "WSLC_RUN_INTEGRATION";

    public IntegrationFactAttribute(
        [CallerFilePath] string? sourceFilePath = null,
        [CallerLineNumber] int sourceLineNumber = -1)
        : base(sourceFilePath, sourceLineNumber)
    {
        if (Environment.GetEnvironmentVariable(EnvironmentVariable) != "1")
        {
            Skip = $"Set {EnvironmentVariable}=1 to run WSL container integration tests.";
        }
    }
}
