#pragma once

#include "wslc/endpoint.hpp"
#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <optional>
#include <string>

namespace wslc::modules
{

class ArtemisContainer;

/// <summary>
/// Testcontainers-style builder for ActiveMQ Artemis. Encapsulates the Image, port and readiness
/// waits so tests don't memorize them. The Image creates its broker instance on first start and
/// the generated configuration exposes MQTT on 1883, so the preset supplies no <c>broker.xml</c>.
/// Anonymous clients are accepted by default; <c>WithCredentials</c> also creates a named user for
/// the connection string.
/// </summary>
class ArtemisBuilder : public WslModuleBuilder<ArtemisBuilder>
{
public:
    /// <summary>Initializes the builder with the default Artemis Image and readiness preset.</summary>
    ArtemisBuilder();

    /// <summary>
    /// Creates the broker instance with this user and password and renders them into the
    /// connection string as userinfo. Without this call clients connect anonymously. The username
    /// must not be blank.
    /// </summary>
    ArtemisBuilder& WithCredentials(std::string username, std::string password);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    ArtemisContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

private:
    std::string m_username = "artemis";
    std::string m_password = "artemis";
    bool m_credentialsInUri = false;
};

/// <summary>
/// Typed ActiveMQ Artemis container: a module wrapper that renders an MQTT URL from the dynamic
/// Host port mapping.
/// </summary>
class ArtemisContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Artemis accepts MQTT client connections on by default (1883).</summary>
    static constexpr int DefaultPort = 1883;

    ArtemisContainer(const ArtemisContainer&) = delete;
    ArtemisContainer& operator=(const ArtemisContainer&) = delete;
    ArtemisContainer(ArtemisContainer&&) noexcept = default;
    ArtemisContainer& operator=(ArtemisContainer&&) noexcept = default;

    /// <summary>
    /// Renders an MQTT URL for the running container, e.g. mqtt://127.0.0.1:49153 for the default
    /// anonymous access, or mqtt://user:pass@127.0.0.1:49153 when WithCredentials configured
    /// credentials.
    /// </summary>
    std::string GetConnectionString() const;

    /// <summary>
    /// Renders an MQTT URL for a caller-supplied endpoint, e.g. an address in front of the
    /// container such as a fault-relay listener. IPv6 hosts are bracketed. Credentials are
    /// percent-encoded when configured.
    /// </summary>
    std::string GetConnectionString(WslEndpoint endpoint) const;

private:
    friend class ArtemisBuilder;
    ArtemisContainer(WslContainer inner, std::optional<std::string> username, std::optional<std::string> password);

    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
};

} // namespace wslc::modules
