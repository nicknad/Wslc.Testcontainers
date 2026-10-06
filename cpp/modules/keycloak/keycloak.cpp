#include "wslc/modules/keycloak.hpp"

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

KeycloakBuilder::KeycloakBuilder()
    : WslModuleBuilder<KeycloakBuilder>("docker.io/keycloak/keycloak:26.8", KeycloakContainer::DefaultPort, "")
{
}

KeycloakBuilder& KeycloakBuilder::WithAdminUsername(std::string adminUsername)
{
    if (adminUsername.empty())
    {
        throw WslException("Admin username must not be empty.");
    }

    m_adminUsername = std::move(adminUsername);
    return *this;
}

KeycloakBuilder& KeycloakBuilder::WithAdminPassword(std::string adminPassword)
{
    if (adminPassword.empty())
    {
        throw WslException("Admin password must not be empty.");
    }

    m_adminPassword = std::move(adminPassword);
    return *this;
}

KeycloakContainer KeycloakBuilder::Build()
{
    return KeycloakContainer(BuildContainer(), m_adminUsername, m_adminPassword);
}

WslContainerBuilder& KeycloakBuilder::Configure(WslContainerBuilder& builder)
{
    return builder.WithEnvironment("KC_BOOTSTRAP_ADMIN_USERNAME", m_adminUsername)
        .WithEnvironment("KC_BOOTSTRAP_ADMIN_PASSWORD", m_adminPassword)
        // The Image entrypoint is kc.sh; the configured command becomes its CMD (start-dev).
        .WithCommand("start-dev")
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/realms/master", KeycloakContainer::DefaultPort));
}

KeycloakContainer::KeycloakContainer(WslContainer inner, std::string adminUsername, std::string adminPassword)
    : WslModuleContainer(std::move(inner)), m_adminUsername(std::move(adminUsername)),
      m_adminPassword(std::move(adminPassword))
{
}

std::string KeycloakContainer::GetEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
