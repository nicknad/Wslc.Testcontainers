namespace Wslc.Testcontainers;

/// <summary>
/// Result of executing a command inside a WSLC environment. Immutable: the exit code and captured
/// streams are set once at construction and cannot be reassigned or mutated with <c>with</c>.
/// </summary>
public sealed record ExecResult
{
    /// <summary>Initializes a result from the process exit code and captured streams.</summary>
    /// <param name="exitCode">The Linux process exit code.</param>
    /// <param name="stdout">Captured standard output.</param>
    /// <param name="stderr">Captured standard error.</param>
    public ExecResult(int exitCode, string stdout, string stderr)
    {
        ExitCode = exitCode;
        Stdout = stdout;
        Stderr = stderr;
    }

    /// <summary>The Linux process exit code.</summary>
    public int ExitCode { get; }

    /// <summary>Captured standard output.</summary>
    public string Stdout { get; }

    /// <summary>Captured standard error.</summary>
    public string Stderr { get; }

    /// <summary>Gets a value indicating whether the command exited successfully.</summary>
    public bool Succeeded => ExitCode == 0;

    /// <summary>Deconstructs the result into its exit code and captured streams.</summary>
    public void Deconstruct(out int exitCode, out string stdout, out string stderr)
    {
        exitCode = ExitCode;
        stdout = Stdout;
        stderr = Stderr;
    }

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
        $"ExecResult {{ ExitCode = {ExitCode}, Stdout.Length = {Stdout.Length}, Stderr.Length = {Stderr.Length} }}";

    private static string Truncate(string value, int maxChars = 4096)
    {
        if (value.Length <= maxChars)
        {
            return value;
        }

        return string.Concat(value.AsSpan(0, maxChars), $"{Environment.NewLine}... (truncated, {value.Length - maxChars} chars omitted)");
    }
}
