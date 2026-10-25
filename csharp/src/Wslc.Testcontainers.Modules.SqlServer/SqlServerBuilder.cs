using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.SqlServer;

/// <summary>
/// Testcontainers-style builder for SQL Server Developer. Encapsulates the image, SA password,
/// port and readiness waits so tests don't memorize them. Defaults are for local tests only;
/// override <see cref="WithPassword"/> for anything shared.
/// </summary>
public sealed class SqlServerBuilder : WslModuleBuilder<SqlServerBuilder>
{
    // SQL Server enforces a complexity policy (at least 8 characters from three of four
    // categories), so this default cannot be the usual "secret" used by the other modules.
    private string _password = "yourStrong(!)Password";

    /// <summary>Initializes the builder with the default SQL Server Developer image and readiness preset.</summary>
    public SqlServerBuilder()
        : base(
            "mcr.microsoft.com/mssql/server:2022-latest",
            SqlServerContainer.DefaultPort,
            string.Empty)
    {
    }

    /// <summary>
    /// Sets the <c>MSSQL_SA_PASSWORD</c> value. Defaults to <c>yourStrong(!)Password</c>; override
    /// for anything shared. The value must satisfy the SQL Server password policy.
    /// </summary>
    public SqlServerBuilder WithPassword(string password)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(password);
        _password = password;
        return this;
    }

    /// <summary>Builds the container with the configured password. The container is not started.</summary>
    public SqlServerContainer Build() => new(BuildContainer(), _password);

    // sqlservr logs readiness slightly before it accepts logins, so probe with sqlcmd instead of
    // the log message. The image ships sqlcmd 18 in /opt/mssql-tools18; older images use
    // /opt/mssql-tools. The password travels as a positional argument, not interpolated into the
    // shell script, so shell metacharacters cannot break out.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        builder
            .WithEnvironment("ACCEPT_EULA", "Y")
            .WithEnvironment("MSSQL_SA_PASSWORD", _password)
            .WithEnvironment("MSSQL_PID", "Developer")
            // sqlservr refuses to start with less than 2000 MB available; the session cap leaves
            // headroom for the kernel and the runtime's own processes.
            .WithMemoryMegabytes(4096)
            // WSLC never runs the image ENTRYPOINT/CMD automatically, so declare sqlservr.
            .WithCommand("/opt/mssql/bin/sqlservr")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .Until("SQL Server accepts connections", async (target, cancellationToken) =>
                    {
                        var result = await target.ExecAsync(
                            "/bin/sh",
                            [
                                "-c",
                                "if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then cmd=/opt/mssql-tools18/bin/sqlcmd; " +
                                "else cmd=/opt/mssql-tools/bin/sqlcmd; fi; exec \"$cmd\" -S localhost -U sa " +
                                "-P \"$1\" -C -b -r 1 -Q 'SELECT 1'",
                                "sh",
                                _password,
                            ],
                            cancellationToken).ConfigureAwait(false);
                        return result.ExitCode == 0;
                    }));

        return builder;
    }
}
