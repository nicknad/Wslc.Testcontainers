#include "wslc/modules/nats.hpp"

#include "wslc/exceptions.hpp"

#include <utility>
#include <vector>

namespace wslc::modules
{

NatsBuilder::NatsBuilder()
    : WslModuleBuilder<NatsBuilder>("docker.io/library/nats:2-alpine", NatsContainer::DefaultPort, "Server is ready")
{
}

NatsBuilder& NatsBuilder::WithJetStream(bool enabled)
{
    m_jetStream = enabled;
    return *this;
}

NatsBuilder& NatsBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

NatsBuilder& NatsBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

NatsContainer NatsBuilder::Build()
{
    if (m_username.has_value() != m_password.has_value())
    {
        throw WslException("NATS credentials need both WithUsername and WithPassword.");
    }

    return NatsContainer(BuildContainer(), m_username, m_password);
}

WslContainerBuilder& NatsBuilder::Configure(WslContainerBuilder& builder)
{
    std::vector<std::string> arguments;
    if (m_jetStream)
    {
        arguments.emplace_back("-js");
    }

    if (m_username.has_value())
    {
        arguments.emplace_back("--user");
        arguments.push_back(*m_username);
        arguments.emplace_back("--pass");
        arguments.push_back(*m_password);
    }

    // The Image entrypoint maps the configured command to its CMD (nats-server).
    return builder.WithCommand("nats-server", std::move(arguments));
}

NatsContainer::NatsContainer(WslContainer inner, std::optional<std::string> username,
                             std::optional<std::string> password)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password))
{
}

std::string NatsContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    if (!m_username.has_value())
    {
        return "nats://" + FormatEndpoint(endpoint);
    }

    return "nats://" + PercentEncode(*m_username) + ":" + PercentEncode(*m_password) + "@" + FormatEndpoint(endpoint);
}

} // namespace wslc::modules
