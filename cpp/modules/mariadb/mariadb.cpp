#include "wslc/modules/mariadb.hpp"

#include "wslc/exceptions.hpp"

#include <utility>

namespace wslc::modules
{

MariaDbBuilder::MariaDbBuilder()
    : WslModuleBuilder<MariaDbBuilder>("docker.io/library/mariadb:11.4", MariaDbContainer::DefaultPort,
                                       "ready for connections")
{
}

MariaDbBuilder& MariaDbBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

MariaDbBuilder& MariaDbBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

MariaDbBuilder& MariaDbBuilder::WithDatabase(std::string database)
{
    if (database.empty())
    {
        throw WslException("Database must not be empty.");
    }

    m_database = std::move(database);
    return *this;
}

MariaDbContainer MariaDbBuilder::Build()
{
    return MariaDbContainer(BuildContainer(), m_username, m_password, m_database);
}

WslContainerBuilder& MariaDbBuilder::Configure(WslContainerBuilder& builder)
{
    builder.WithEnvironment("MARIADB_ROOT_PASSWORD", m_password)
        .WithEnvironment("MARIADB_DATABASE", m_database)
        // Replicates the Image ENTRYPOINT/CMD (docker-entrypoint.sh mariadbd). The container
        // init process replaces the keep-alive shell; without this the server never starts and
        // readiness can never succeed.
        .WithCommand("/usr/local/bin/docker-entrypoint.sh", {"mariadbd"});

    // The entrypoint rejects MARIADB_USER=root; that account is created by
    // MARIADB_ROOT_PASSWORD alone.
    if (m_username != "root")
    {
        builder.WithEnvironment("MARIADB_USER", m_username).WithEnvironment("MARIADB_PASSWORD", m_password);
    }

    return builder;
}

MariaDbContainer::MariaDbContainer(WslContainer inner, std::string username, std::string password, std::string database)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password)),
      m_database(std::move(database))
{
}

std::string MariaDbContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "Server=" + endpoint.Host + ";Port=" + std::to_string(endpoint.Port) + ";User ID=" + m_username +
           ";Password=" + m_password + ";Database=" + m_database;
}

} // namespace wslc::modules
