#include "wslc/modules/postgresql.hpp"

#include "wslc/exceptions.hpp"

#include <utility>

namespace wslc::modules
{

PostgreSqlBuilder::PostgreSqlBuilder()
    : WslModuleBuilder<PostgreSqlBuilder>("docker.io/library/postgres:15-alpine", PostgreSqlContainer::ContainerPort,
                                          "database system is ready to accept connections")
{
}

PostgreSqlBuilder& PostgreSqlBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslcException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

PostgreSqlBuilder& PostgreSqlBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

PostgreSqlBuilder& PostgreSqlBuilder::WithDatabase(std::string database)
{
    if (database.empty())
    {
        throw WslcException("Database must not be empty.");
    }

    m_database = std::move(database);
    return *this;
}

PostgreSqlContainer PostgreSqlBuilder::Build()
{
    return PostgreSqlContainer(BuildContainer(), m_username, m_password, m_database);
}

WslContainerBuilder& PostgreSqlBuilder::Configure(WslContainerBuilder& builder)
{
    return builder.WithEnvironment("POSTGRES_USER", m_username)
        .WithEnvironment("POSTGRES_PASSWORD", m_password)
        .WithEnvironment("POSTGRES_DB", m_database)
        // Replicates the Image ENTRYPOINT/CMD (docker-entrypoint.sh postgres). The container
        // init process replaces the keep-alive shell; without this the server never starts and
        // readiness can never succeed.
        .WithCommand("/usr/local/bin/docker-entrypoint.sh", {"postgres"});
}

PostgreSqlContainer::PostgreSqlContainer(WslContainer inner, std::string username, std::string password,
                                         std::string database)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password)),
      m_database(std::move(database))
{
}

std::string PostgreSqlContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(ContainerPort);
    return "Host=" + endpoint.Host + ";Port=" + std::to_string(endpoint.Port) + ";Username=" + m_username +
           ";Password=" + m_password + ";Database=" + m_database;
}

} // namespace wslc::modules
