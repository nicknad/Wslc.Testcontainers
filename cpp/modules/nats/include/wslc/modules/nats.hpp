#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <optional>
#include <string>

namespace wslc::modules
{

class NatsContainer;

/// <summary>
/// Testcontainers-style builder for NATS. Encapsulates the Image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; set both
/// WithUsername and WithPassword for shared instances.
/// </summary>
class NatsBuilder : public WslModuleBuilder<NatsBuilder>
{
public:
    /// <summary>Initializes the builder with the default NATS Image and readiness preset.</summary>
    NatsBuilder();

    /// <summary>Enables JetStream persistence (the -js flag). Off by default.</summary>
    NatsBuilder& WithJetStream(bool enabled = true);

    /// <summary>Requires clients to authenticate with this user. Requires WithPassword.</summary>
    NatsBuilder& WithUsername(std::string username);

    /// <summary>Sets the password for WithUsername.</summary>
    NatsBuilder& WithPassword(std::string password);

    /// <summary>Builds the container with the configured options. The container is not started.</summary>
    NatsContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

private:
    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
    bool m_jetStream = false;
};

/// <summary>
/// Typed NATS container: renders a NATS client connection URL from the dynamic Host port
/// mapping.
/// </summary>
class NatsContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port NATS accepts client connections on by default (4222).</summary>
    static constexpr int DefaultPort = 4222;

    NatsContainer(const NatsContainer&) = delete;
    NatsContainer& operator=(const NatsContainer&) = delete;
    NatsContainer(NatsContainer&&) noexcept = default;
    NatsContainer& operator=(NatsContainer&&) noexcept = default;

    /// <summary>
    /// Renders a NATS client URL for the running container, e.g. nats://127.0.0.1:49153 or,
    /// with credentials, nats://user:password@127.0.0.1:49153.
    /// </summary>
    std::string GetConnectionString() const;

private:
    friend class NatsBuilder;
    NatsContainer(WslContainer inner, std::optional<std::string> username, std::optional<std::string> password);

    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
};

} // namespace wslc::modules
