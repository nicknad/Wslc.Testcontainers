#include "wslc/modules/vault.hpp"

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

VaultBuilder::VaultBuilder()
    : WslModuleBuilder<VaultBuilder>("docker.io/hashicorp/vault:2.1", VaultContainer::DefaultPort, "")
{
}

VaultBuilder& VaultBuilder::WithRootToken(std::string rootToken)
{
    if (rootToken.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw WslException("Root token must not be empty.");
    }

    m_rootToken = std::move(rootToken);
    return *this;
}

VaultContainer VaultBuilder::Build()
{
    return VaultContainer(BuildContainer(), m_rootToken);
}

WslContainerBuilder& VaultBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image entrypoint maps the configured command to its CMD: vault server.
    // Dev mode starts unsealed and ready, which is what tests need.
    return builder
        .WithCommand("server", {"-dev", "-dev-root-token-id=" + m_rootToken,
                                "-dev-listen-address=0.0.0.0:" + std::to_string(VaultContainer::DefaultPort)})
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/v1/sys/health", VaultContainer::DefaultPort));
}

VaultContainer::VaultContainer(WslContainer inner, std::string rootToken)
    : WslModuleContainer(std::move(inner)), m_rootToken(std::move(rootToken))
{
}

std::string VaultContainer::GetAddress() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
