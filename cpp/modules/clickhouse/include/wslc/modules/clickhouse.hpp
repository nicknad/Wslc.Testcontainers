#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class ClickHouseContainer;

/// <summary>
/// Testcontainers-style builder for ClickHouse. Encapsulates the Image, credentials, ports and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// the username/password/database for anything shared.
/// </summary>
class ClickHouseBuilder : public WslModuleBuilder<ClickHouseBuilder>
{
public:
    /// <summary>Initializes the builder with the default ClickHouse Image and readiness preset.</summary>
    ClickHouseBuilder();

    /// <summary>Sets the CLICKHOUSE_USER value. Defaults to "default".</summary>
    ClickHouseBuilder& WithUsername(std::string username);

    /// <summary>Sets the CLICKHOUSE_PASSWORD value. Defaults to an empty password; override for anything
    /// shared.</summary>
    ClickHouseBuilder& WithPassword(std::string password);

    /// <summary>Sets the CLICKHOUSE_DB value. Defaults to "default".</summary>
    ClickHouseBuilder& WithDatabase(std::string database);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    ClickHouseContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // ClickHouse logs to files rather than a stable stdout readiness line, so readiness is the
    // /ping HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }

private:
    std::string m_username = "default";
    std::string m_password;
    std::string m_database = "default";
};

/// <summary>
/// Typed ClickHouse container: renders a ClickHouse.Client-style connection string from the
/// dynamic host port mapping.
/// </summary>
class ClickHouseContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port the ClickHouse HTTP interface listens on by default (8123).</summary>
    static constexpr int HttpPort = 8123;

    /// <summary>The Linux TCP port the ClickHouse native protocol listens on by default (9000).</summary>
    static constexpr int NativePort = 9000;

    ClickHouseContainer(const ClickHouseContainer&) = delete;
    ClickHouseContainer& operator=(const ClickHouseContainer&) = delete;
    ClickHouseContainer(ClickHouseContainer&&) noexcept = default;
    ClickHouseContainer& operator=(ClickHouseContainer&&) noexcept = default;

    /// <summary>Renders a ClickHouse.Client connection string for the running container's HTTP port, e.g.
    /// Host=127.0.0.1;Port=49153;Username=default;Password=;Database=default.</summary>
    std::string GetConnectionString() const;

private:
    friend class ClickHouseBuilder;
    ClickHouseContainer(WslContainer inner, std::string username, std::string password, std::string database);

    std::string m_username;
    std::string m_password;
    std::string m_database;
};

} // namespace wslc::modules
