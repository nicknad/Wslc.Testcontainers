namespace Wslc.Testcontainers;

/// <summary>Optional settings for <c>ExecAsync</c> on <see cref="IWslContainer"/>.</summary>
/// <remarks>
/// Pass <c>null</c> instead of <c>new ExecOptions()</c> for defaults.
/// <see cref="StandardInput"/> and <see cref="Timeout"/> apply only to <c>ExecAsync</c>;
/// passing an <see cref="ExecOptions"/> with either set to <c>StartProcess</c> (which accepts
/// <see cref="ProcessOptions"/>) throws <see cref="ArgumentException"/>.
/// </remarks>
public sealed record ExecOptions : ProcessOptions
{
    /// <summary>Text written to the command standard input. Must not exceed 64 MiB when UTF-8 encoded.</summary>
    public string? StandardInput { get; init; }

    /// <summary>Maximum execution time. Must be positive and at most 24 hours when set. When exceeded a <see cref="WslTimeoutException"/> is thrown.</summary>
    public TimeSpan? Timeout { get; init; }
}
