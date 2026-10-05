namespace Wslc.Testcontainers;

/// <summary>
/// Result of executing a command inside a WSLC environment.
/// </summary>
/// <param name="ExitCode">The Linux process exit code.</param>
/// <param name="Stdout">Captured standard output.</param>
/// <param name="Stderr">Captured standard error.</param>
public sealed record ExecResult(int ExitCode, string Stdout, string Stderr)
{
    /// <summary>Gets a value indicating whether the command exited successfully.</summary>
    public bool Succeeded => ExitCode == 0;

    /// <summary>Throws a <see cref="WslProcessException"/> when the command failed.</summary>
    /// <remarks>
    /// Output is truncated to 4 KiB per stream in the exception message to avoid multi-megabyte
    /// messages; the full <see cref="ExecResult.Stdout"/>/<see cref="ExecResult.Stderr"/> remain
    /// available on this instance (each capped at ~1 MiB by the capture buffer).
    /// </remarks>
    public ExecResult EnsureSuccess()
    {
        if (!Succeeded)
        {
            throw new WslProcessException(
                $"Command failed with exit code {ExitCode}.{Environment.NewLine}stdout:{Environment.NewLine}{Truncate(Stdout)}{Environment.NewLine}stderr:{Environment.NewLine}{Truncate(Stderr)}");
        }

        return this;
    }

    /// <summary>
    /// Returns a bounded summary with the exit code and the stdout/stderr lengths but none of
    /// their contents, so logging a result cannot dump megabytes of captured output.
    /// </summary>
    public override string ToString() =>
        $"ExecResult {{ ExitCode = {ExitCode}, Stdout.Length = {Stdout?.Length ?? 0}, Stderr.Length = {Stderr?.Length ?? 0} }}";

    private static string Truncate(string value, int maxChars = 4096)
    {
        if (value.Length <= maxChars)
        {
            return value;
        }

        return string.Concat(value.AsSpan(0, maxChars), $"{Environment.NewLine}... (truncated, {value.Length - maxChars} chars omitted)");
    }
}
