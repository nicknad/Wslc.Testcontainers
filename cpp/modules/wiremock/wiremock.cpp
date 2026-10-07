#include "wslc/modules/wiremock.hpp"

#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

WireMockBuilder::WireMockBuilder()
    : WslModuleBuilder<WireMockBuilder>("docker.io/wiremock/wiremock:3x", WireMockContainer::DefaultPort, "")
{
}

WireMockContainer WireMockBuilder::Build()
{
    return WireMockContainer(BuildContainer());
}

WslContainerBuilder& WireMockBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image entrypoint prepends the WireMock Java command when the configured command starts
    // with a flag, so --verbose enables verbose logging.
    return builder.WithCommand("--verbose")
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/__admin/health", WireMockContainer::DefaultPort));
}

WireMockContainer::WireMockContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string WireMockContainer::GetEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
