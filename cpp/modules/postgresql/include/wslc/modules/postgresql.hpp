#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class PostgreSqlContainer;

/// <summary>
/// Testcontainers-style builder for Postgres. Encapsulates the Image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// the credentials for anything shared.
/// </summary>
class PostgreSqlBuilder : public WslModuleBuilder<PostgreSqlBuilder>
{
public:
    /// <summary>Initializes the builder with the default Postgres Image and readiness preset.</summary>
    PostgreSqlBuilder();

    /// <summary>Sets the POSTGRES_USER value. Defaults to "postgres".</summary>
    PostgreSqlBuilder& WithUsername(std::string username);

    /// <summary>Sets the POSTGRES_PASSWORD value. Defaults to "secret"; override for anything shared.</summary>
    PostgreSqlBuilder& WithPassword(std::string password);

    /// <summary>Sets the POSTGRES_DB value. Defaults to "customers".</summary>
    PostgreSqlBuilder& WithDatabase(std::string database);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    PostgreSqlContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // The Postgres entrypoint starts a temporary server to Run init scripts, Logs readiness,
    // then shuts it down and starts the real server. Accepting the first occurrence lets the
    // test connect to the temp server and get dropped mid-initialization, so require the
    // second, real readiness message.
    int ReadyMessageOccurrences() const override { return 2; }

private:
    std::string m_username = "postgres";
    std::string m_password = "secret";
    std::string m_database = "customers";
};

/// <summary>
/// Typed Postgres container: renders a connection string from the dynamic Host port mapping.
/// </summary>
class PostgreSqlContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Postgres listens on (5432).</summary>
    static constexpr int ContainerPort = 5432;

    PostgreSqlContainer(const PostgreSqlContainer&) = delete;
    PostgreSqlContainer& operator=(const PostgreSqlContainer&) = delete;
    PostgreSqlContainer(PostgreSqlContainer&&) noexcept = default;
    PostgreSqlContainer& operator=(PostgreSqlContainer&&) noexcept = default;

    /// <summary>Gets the Windows port mapped to the Postgres port.</summary>
    int GetMappedPort() const { return WslModuleContainer::GetConnectEndpoint(ContainerPort).Port; }

    /// <summary>Renders an Npgsql-style connection string for the running container.</summary>
    std::string GetConnectionString() const;

private:
    friend class PostgreSqlBuilder;
    PostgreSqlContainer(WslContainer inner, std::string username, std::string password, std::string database);

    std::string m_username;
    std::string m_password;
    std::string m_database;
};

} // namespace wslc::modules
