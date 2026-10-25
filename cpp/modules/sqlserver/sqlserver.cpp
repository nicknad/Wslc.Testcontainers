#include "wslc/modules/sqlserver.hpp"

#include <utility>

namespace wslc::modules
{

SqlServerBuilder::SqlServerBuilder()
    : WslModuleBuilder<SqlServerBuilder>("mcr.microsoft.com/mssql/server:2022-latest", SqlServerContainer::DefaultPort,
                                         "")
{
}

SqlServerBuilder& SqlServerBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

SqlServerContainer SqlServerBuilder::Build()
{
    return SqlServerContainer(BuildContainer(), m_password);
}

WslContainerBuilder& SqlServerBuilder::Configure(WslContainerBuilder& builder)
{
    // sqlservr refuses to start with less than 2000 MB available; the session cap leaves headroom
    // for the kernel and the runtime's own processes.
    // WSLC never runs the Image ENTRYPOINT/CMD automatically, so declare sqlservr.
    builder.WithEnvironment("ACCEPT_EULA", "Y")
        .WithEnvironment("MSSQL_SA_PASSWORD", m_password)
        .WithEnvironment("MSSQL_PID", "Developer")
        .WithMemoryMegabytes(4096)
        .WithCommand("/opt/mssql/bin/sqlservr");

    // The Image ships sqlcmd 18 in /opt/mssql-tools18; older Images use /opt/mssql-tools. The
    // password travels as a positional argument, not interpolated into the shell script, so shell
    // metacharacters cannot break out.
    builder.WithWaitStrategy(
        waiting::ForWsl()
            .WithTimeout(WaitTimeout())
            .Until(
                "SQL Server accepts connections",
                [password = m_password](waiting::IWaitTarget& target, std::stop_token token)
                {
                    const ExecResult result = target.Exec(
                        "/bin/sh",
                        {"-c",
                         "if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then cmd=/opt/mssql-tools18/bin/sqlcmd; else "
                         "cmd=/opt/mssql-tools/bin/sqlcmd; fi; exec \"$cmd\" -S localhost -U sa -P \"$1\" -C -b -r 1 "
                         "-Q 'SELECT 1'",
                         "sh", password},
                        token);
                    return result.ExitCode == 0;
                }));

    return builder;
}

SqlServerContainer::SqlServerContainer(WslContainer inner, std::string password)
    : WslModuleContainer(std::move(inner)), m_password(std::move(password))
{
}

std::string SqlServerContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "Server=" + FormatHost(endpoint.Host) + "," + std::to_string(endpoint.Port) +
           ";Database=master;User Id=sa;Password=" + m_password + ";TrustServerCertificate=True";
}

} // namespace wslc::modules
