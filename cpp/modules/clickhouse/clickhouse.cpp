#include "wslc/modules/clickhouse.hpp"

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

ClickHouseBuilder::ClickHouseBuilder()
    : WslModuleBuilder<ClickHouseBuilder>("docker.io/clickhouse/clickhouse-server:26.7", ClickHouseContainer::HttpPort,
                                          "")
{
}

ClickHouseBuilder& ClickHouseBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

ClickHouseBuilder& ClickHouseBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

ClickHouseBuilder& ClickHouseBuilder::WithDatabase(std::string database)
{
    if (database.empty())
    {
        throw WslException("Database must not be empty.");
    }

    m_database = std::move(database);
    return *this;
}

ClickHouseContainer ClickHouseBuilder::Build()
{
    return ClickHouseContainer(BuildContainer(), m_username, m_password, m_database);
}

WslContainerBuilder& ClickHouseBuilder::Configure(WslContainerBuilder& builder)
{
    builder.WithEnvironment("CLICKHOUSE_USER", m_username)
        .WithEnvironment("CLICKHOUSE_DB", m_database)
        // The entrypoint only writes users.d and removes the default user's localhost-only
        // restriction when one of user/password/access-management is set, so opt into access
        // management for the default (empty password) case as well.
        .WithEnvironment("CLICKHOUSE_DEFAULT_ACCESS_MANAGEMENT", "1")
        .WithPort(ClickHouseContainer::NativePort);
    if (!m_password.empty())
    {
        // The entrypoint treats an unset and an empty password the same; leaving the variable
        // out keeps empty-password setups identical to the Image default.
        builder.WithEnvironment("CLICKHOUSE_PASSWORD", m_password);
    }

    // The Image entrypoint only takes its server path (chown, user setup, database init) when it
    // gets no arguments or an argument that starts with "--"; the configured command becomes its
    // first argument. "clickhouse-server" would be exec'd directly and fall back to ClickHouse's
    // embedded config (loopback-only, no users.d), so pass an ignored root flag.
    return builder.WithCommand("--").WithWaitStrategy(
        wslc::waiting::ForWsl()
            .WithTimeout(WaitTimeout())
            .UntilHttpRequestSucceeds("/ping", ClickHouseContainer::HttpPort));
}

ClickHouseContainer::ClickHouseContainer(WslContainer inner, std::string username, std::string password,
                                         std::string database)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password)),
      m_database(std::move(database))
{
}

std::string ClickHouseContainer::GetConnectionString() const
{
    return FormatConnectionString(GetConnectEndpoint(HttpPort), m_username, m_password, m_database);
}

} // namespace wslc::modules
