namespace Wslc.Testcontainers;

/// <summary>Optional settings for <see cref="IWslContainer.ExecAsync(string, string[], ExecOptions, CancellationToken)"/>.</summary>
public sealed record ExecOptions
{
    /// <summary>Environment variables for the command. Overrides container-level variables with the same name.</summary>
    public IReadOnlyDictionary<string, string>? Environment { get; init; }

    /// <summary>Linux working directory for the command.</summary>
    public string? WorkingDirectory { get; init; }

    /// <summary>Text written to the command standard input.</summary>
    public string? StandardInput { get; init; }

    /// <summary>Maximum execution time. When exceeded a <see cref="WslTimeoutException"/> is thrown.</summary>
    public TimeSpan? Timeout { get; init; }
}
