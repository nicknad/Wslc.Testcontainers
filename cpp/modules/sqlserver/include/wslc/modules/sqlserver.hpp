#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class SqlServerContainer;

/// <summary>
/// Testcontainers-style builder for SQL Server Developer. Encapsulates the Image, SA password,
/// port and readiness waits so tests don't memorize them. Defaults are for local tests only;
/// override the password for anything shared.
/// </summary>
class SqlServerBuilder : public WslModuleBuilder<SqlServerBuilder>
{
public:
    /// <summary>Initializes the builder with the default SQL Server Developer Image and readiness preset.</summary>
    SqlServerBuilder();

    /// <summary>
    /// Sets the MSSQL_SA_PASSWORD value. Defaults to "yourStrong(!)Password"; override for anything
    /// shared. The value must satisfy the SQL Server password policy.
    /// </summary>
    SqlServerBuilder& WithPassword(std::string password);

    /// <summary>Builds the container with the configured password. The container is not started.</summary>
    SqlServerContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // sqlservr logs readiness slightly before it accepts logins, so readiness is the sqlcmd probe
    // added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }

private:
    std::string m_password = "yourStrong(!)Password";
};

/// <summary>
/// Typed SQL Server container: renders a Microsoft.Data.SqlClient connection string from the
/// dynamic Host port mapping.
/// </summary>
class SqlServerContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port SQL Server listens on by default (1433).</summary>
    static constexpr int DefaultPort = 1433;

    SqlServerContainer(const SqlServerContainer&) = delete;
    SqlServerContainer& operator=(const SqlServerContainer&) = delete;
    SqlServerContainer(SqlServerContainer&&) noexcept = default;
    SqlServerContainer& operator=(SqlServerContainer&&) noexcept = default;

    /// <summary>
    /// Renders a Microsoft.Data.SqlClient connection string for the running container, e.g.
    /// Server=127.0.0.1,49153;Database=master;User Id=sa;Password=...;TrustServerCertificate=True
    /// The connection targets master; create additional databases with Exec.
    /// </summary>
    std::string GetConnectionString() const;

private:
    friend class SqlServerBuilder;
    SqlServerContainer(WslContainer inner, std::string password);

    std::string m_password;
};

} // namespace wslc::modules
