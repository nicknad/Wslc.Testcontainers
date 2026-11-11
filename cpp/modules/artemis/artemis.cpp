#include "wslc/modules/artemis.hpp"

#include "wslc/exceptions.hpp"

#include <utility>

namespace wslc::modules
{

ArtemisBuilder::ArtemisBuilder()
    // Docker Hub publishes the Alpine Image as 2.57.0-alpine; latest-alpine points at the same
    // digest, so the calendar tag is pinned explicitly.
    : WslModuleBuilder<ArtemisBuilder>("docker.io/apache/artemis:2.57.0-alpine", ArtemisContainer::DefaultPort,
                                       // The broker logs "AMQ221007: Server is now live" once startup
                                       // finishes. The message ID is the version-tolerant tail: the
                                       // surrounding text has changed over releases (2.57.0 says
                                       // "Server is now active"), while AMQ221007 has stayed with the
                                       // readiness message. The earlier "starting" lines never carry
                                       // it, so it cannot pass before the broker accepts clients.
                                       "AMQ221007")
{
}

ArtemisBuilder& ArtemisBuilder::WithCredentials(std::string username, std::string password)
{
    if (username.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    m_password = std::move(password);
    m_credentialsInUri = true;
    return *this;
}

ArtemisContainer ArtemisBuilder::Build()
{
    // The Image creates the broker instance with the environment credentials on first start, but
    // they stay out of the URL until WithCredentials opts in, so the default is anonymous.
    std::optional<std::string> username;
    std::optional<std::string> password;
    if (m_credentialsInUri)
    {
        username = m_username;
        password = m_password;
    }

    return ArtemisContainer(BuildContainer(), std::move(username), std::move(password));
}

WslContainerBuilder& ArtemisBuilder::Configure(WslContainerBuilder& builder)
{
    return builder
        // docker-run.sh feeds these to `artemis create` on first start and to the generated
        // artemis-users.properties.
        .WithEnvironment("ARTEMIS_USER", m_username)
        .WithEnvironment("ARTEMIS_PASSWORD", m_password)
        // The Image defaults to --require-login; tests connect without a user by default.
        .WithEnvironment("ANONYMOUS_LOGIN", "true")
        // The Image WORKDIR: docker-run.sh creates ./etc and ./bin relative to it.
        .WithWorkingDirectory("/var/lib/artemis-instance")
        // The Image ENTRYPOINT (/docker-run.sh) creates the broker instance and execs
        // ./bin/artemis. WSLC keeps that ENTRYPOINT and appends the configured command as its
        // CMD, so the command is only the argument "run"; passing the script path here would make
        // docker-run.sh invoke itself instead of the broker.
        .WithCommand("run");
}

ArtemisContainer::ArtemisContainer(WslContainer inner, std::optional<std::string> username,
                                   std::optional<std::string> password)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password))
{
}

std::string ArtemisContainer::GetConnectionString() const
{
    return GetConnectionString(GetConnectEndpoint(DefaultPort));
}

std::string ArtemisContainer::GetConnectionString(WslEndpoint endpoint) const
{
    if (!m_username.has_value())
    {
        return "mqtt://" + FormatEndpoint(endpoint);
    }

    return "mqtt://" + PercentEncode(*m_username) + ":" + PercentEncode(*m_password) + "@" + FormatEndpoint(endpoint);
}

} // namespace wslc::modules
