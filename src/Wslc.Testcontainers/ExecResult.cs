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
    public ExecResult EnsureSuccess()
    {
        if (!Succeeded)
        {
            throw new WslProcessException(
                $"Command failed with exit code {ExitCode}.{Environment.NewLine}stdout:{Environment.NewLine}{Stdout}{Environment.NewLine}stderr:{Environment.NewLine}{Stderr}");
        }

        return this;
    }
}
