namespace Wslc.Testcontainers;

/// <summary>Optional settings for <see cref="IWslContainer.ExecAsync(string, string[], ExecOptions?, CancellationToken)"/>.</summary>
/// <remarks>
/// Pass <c>null</c> instead of <c>new ExecOptions()</c> for defaults.
/// <c>StandardInput</c> and <c>Timeout</c> apply only to <c>ExecAsync</c>; <c>StartProcess</c>
/// rejects them with <see cref="ArgumentException"/>.
/// </remarks>
public sealed record ExecOptions
{
    /// <summary>Environment variables for the command. Overrides container-level variables with the same name. Names must be valid Linux identifiers; values must not be null.</summary>
    public IReadOnlyDictionary<string, string>? Environment { get; init; }

    /// <summary>Linux working directory for the command. Must be non-empty when set.</summary>
    public string? WorkingDirectory { get; init; }

    /// <summary>Text written to the command standard input (ExecAsync only).</summary>
    public string? StandardInput { get; init; }

    /// <summary>Maximum execution time. Must be positive when set. When exceeded a <see cref="WslTimeoutException"/> is thrown.</summary>
    public TimeSpan? Timeout { get; init; }
}
