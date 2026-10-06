#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <optional>
#include <string>

namespace wslc::modules
{

class MongoDbContainer;

/// <summary>
/// Testcontainers-style builder for MongoDB. Encapsulates the Image, credentials, port and
/// readiness waits so tests don't memorize them. The default is a single server without
/// authentication; set both WithUsername and WithPassword to create the root user instead.
/// Defaults are for local tests only.
/// </summary>
class MongoDbBuilder : public WslModuleBuilder<MongoDbBuilder>
{
public:
    /// <summary>Initializes the builder with the default MongoDB Image and readiness preset.</summary>
    MongoDbBuilder();

    /// <summary>Creates the root user with this name (in the admin database). Requires WithPassword.</summary>
    MongoDbBuilder& WithUsername(std::string username);

    /// <summary>Sets the root password. Requires WithUsername.</summary>
    MongoDbBuilder& WithPassword(std::string password);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    MongoDbContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // With credentials the entrypoint starts a temporary server to create the root user, logs
    // readiness, shuts it down, and starts the real server. Accepting the first occurrence would
    // let the test connect to the temp server and get dropped mid-initialization, so require the
    // second, real readiness message. Without credentials the single server logs it once.
    int ReadyMessageOccurrences() const override { return m_username.has_value() ? 2 : 1; }

private:
    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
};

/// <summary>
/// Typed MongoDB container: renders a MongoDB driver connection string from the dynamic Host
/// port mapping.
/// </summary>
class MongoDbContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port MongoDB listens on by default (27017).</summary>
    static constexpr int DefaultPort = 27017;

    MongoDbContainer(const MongoDbContainer&) = delete;
    MongoDbContainer& operator=(const MongoDbContainer&) = delete;
    MongoDbContainer(MongoDbContainer&&) noexcept = default;
    MongoDbContainer& operator=(MongoDbContainer&&) noexcept = default;

    /// <summary>
    /// Renders a MongoDB driver connection string for the running container, e.g.
    /// mongodb://127.0.0.1:49153 or, with credentials,
    /// mongodb://user:password@127.0.0.1:49153/?authSource=admin.
    /// </summary>
    std::string GetConnectionString() const;

private:
    friend class MongoDbBuilder;
    MongoDbContainer(WslContainer inner, std::optional<std::string> username, std::optional<std::string> password);

    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
};

} // namespace wslc::modules
