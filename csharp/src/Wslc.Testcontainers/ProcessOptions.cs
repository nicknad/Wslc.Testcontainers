namespace Wslc.Testcontainers;

/// <summary>Optional settings shared by <c>ExecAsync</c> and <c>StartProcess</c> on <see cref="IWslContainer"/>.</summary>
/// <remarks>
/// Pass <c>null</c> instead of <c>new ProcessOptions()</c> for defaults.
/// <see cref="ExecOptions"/> derives from this type, but <c>StartProcess</c> rejects an
/// <see cref="ExecOptions"/> carrying <see cref="ExecOptions.StandardInput"/> or
/// <see cref="ExecOptions.Timeout"/> with <see cref="ArgumentException"/>; those settings
/// apply only to <c>ExecAsync</c>.
/// </remarks>
public record ProcessOptions
{
    /// <summary>Environment variables for the command. Overrides container-level variables with the same name. Names must be valid Linux identifiers; values must not be null.</summary>
    public IReadOnlyDictionary<string, string>? Environment { get; init; }

    /// <summary>Linux working directory for the command. Must be non-empty when set.</summary>
    public string? WorkingDirectory { get; init; }
}
