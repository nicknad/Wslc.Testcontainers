#include "wslc/modules/mailpit.hpp"

#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

MailPitBuilder::MailPitBuilder()
    : WslModuleBuilder<MailPitBuilder>("docker.io/axllent/mailpit:v1.31", MailPitContainer::SmtpPort, "")
{
}

MailPitContainer MailPitBuilder::Build()
{
    return MailPitContainer(BuildContainer());
}

WslContainerBuilder& MailPitBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image entrypoint is the mailpit binary itself, so the configured command is its CMD:
    // a root flag starts the server (and keeps tests from phoning home).
    return builder.WithCommand("--disable-version-check")
        .WithPort(MailPitContainer::HttpPort)
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/livez", MailPitContainer::HttpPort));
}

MailPitContainer::MailPitContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string MailPitContainer::GetSmtpEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(SmtpPort);
    return endpoint.Host + ":" + std::to_string(endpoint.Port);
}

std::string MailPitContainer::GetHttpEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(HttpPort);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
