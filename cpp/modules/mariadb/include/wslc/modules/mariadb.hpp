#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class MariaDbContainer;

/// <summary>
/// Testcontainers-style builder for MariaDB. Encapsulates the Image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// the credentials for anything shared.
/// </summary>
class MariaDbBuilder : public WslModuleBuilder<MariaDbBuilder>
{
public:
    /// <summary>Initializes the builder with the default MariaDB Image and readiness preset.</summary>
    MariaDbBuilder();

    /// <summary>Sets the MARIADB_USER value. Defaults to "mariadb"; use "root" for the server root account.</summary>
    MariaDbBuilder& WithUsername(std::string username);

    /// <summary>Sets the password for the configured user (and the root account). Defaults to "secret"; override for
    /// anything shared.</summary>
    MariaDbBuilder& WithPassword(std::string password);

    /// <summary>Sets the MARIADB_DATABASE value. Defaults to "customers".</summary>
    MariaDbBuilder& WithDatabase(std::string database);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    MariaDbContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Like the Postgres entrypoint, MariaDB starts a temporary server to Run init scripts,
    // Logs readiness, shuts it down, and starts the real server. Accepting the first
    // occurrence would let the test connect to the temp server and get dropped
    // mid-initialization, so require the second, real readiness message.
    int ReadyMessageOccurrences() const override { return 2; }

private:
    std::string m_username = "mariadb";
    std::string m_password = "secret";
    std::string m_database = "customers";
};

/// <summary>
/// Typed MariaDB container: renders a MySqlConnector-style connection string from the dynamic
/// Host port mapping.
/// </summary>
class MariaDbContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port MariaDB listens on by default (3306).</summary>
    static constexpr int DefaultPort = 3306;

    MariaDbContainer(const MariaDbContainer&) = delete;
    MariaDbContainer& operator=(const MariaDbContainer&) = delete;
    MariaDbContainer(MariaDbContainer&&) noexcept = default;
    MariaDbContainer& operator=(MariaDbContainer&&) noexcept = default;

    /// <summary>
    /// Renders a MySqlConnector-style connection string for the running container, e.g.
    /// Server=127.0.0.1;Port=49153;User ID=...;Password=...;Database=...
    /// </summary>
    std::string GetConnectionString() const;

private:
    friend class MariaDbBuilder;
    MariaDbContainer(WslContainer inner, std::string username, std::string password, std::string database);

    std::string m_username;
    std::string m_password;
    std::string m_database;
};

} // namespace wslc::modules
