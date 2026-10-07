#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class KeycloakContainer;

/// <summary>
/// Testcontainers-style builder for Keycloak. Encapsulates the Image, admin credentials, port
/// and readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// the admin username/password for anything shared.
/// </summary>
class KeycloakBuilder : public WslModuleBuilder<KeycloakBuilder>
{
public:
    /// <summary>Initializes the builder with the default Keycloak Image and readiness preset.</summary>
    KeycloakBuilder();

    /// <summary>Sets the KC_BOOTSTRAP_ADMIN_USERNAME value. Defaults to "admin".</summary>
    KeycloakBuilder& WithAdminUsername(std::string adminUsername);

    /// <summary>Sets the KC_BOOTSTRAP_ADMIN_PASSWORD value. Defaults to "admin"; override for anything
    /// shared.</summary>
    KeycloakBuilder& WithAdminPassword(std::string adminPassword);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    KeycloakContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Keycloak does not emit a stable stdout readiness line before the HTTP listener accepts
    // requests, so readiness is the /realms/master HTTP check added in Configure instead of a
    // log message.
    int ReadyMessageOccurrences() const override { return 0; }

private:
    std::string m_adminUsername = "admin";
    std::string m_adminPassword = "admin";
};

/// <summary>
/// Typed Keycloak container: renders the HTTP base URL and the bootstrap admin credentials from
/// the dynamic host port mapping.
/// </summary>
class KeycloakContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Keycloak serves HTTP on by default (8080).</summary>
    static constexpr int DefaultPort = 8080;

    KeycloakContainer(const KeycloakContainer&) = delete;
    KeycloakContainer& operator=(const KeycloakContainer&) = delete;
    KeycloakContainer(KeycloakContainer&&) noexcept = default;
    KeycloakContainer& operator=(KeycloakContainer&&) noexcept = default;

    /// <summary>Renders the Keycloak HTTP base URL (e.g. http://127.0.0.1:49153).</summary>
    std::string GetEndpoint() const;

    /// <summary>The bootstrap admin username clients must authenticate with.</summary>
    std::string AdminUsername() const { return m_adminUsername; }

    /// <summary>The bootstrap admin password clients must authenticate with.</summary>
    std::string AdminPassword() const { return m_adminPassword; }

private:
    friend class KeycloakBuilder;
    KeycloakContainer(WslContainer inner, std::string adminUsername, std::string adminPassword);

    std::string m_adminUsername;
    std::string m_adminPassword;
};

} // namespace wslc::modules
