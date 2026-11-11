#include "wslc/modules/mosquitto.hpp"

#include <utility>

namespace wslc::modules
{

MosquittoBuilder::MosquittoBuilder()
    // Docker Hub publishes the Alpine image as 2.1.2-alpine; there is no plain 2.1.2 tag.
    : WslModuleBuilder<MosquittoBuilder>("docker.io/library/eclipse-mosquitto:2.1.2-alpine",
                                         MosquittoContainer::DefaultPort,
                                         // Mosquitto logs "mosquitto version <version> running" once its
                                         // listener sockets are open. The version-independent tail is the
                                         // readiness token: the earlier "starting" line does not contain it, so
                                         // it cannot pass before the broker accepts clients.
                                         "running")
{
}

MosquittoContainer MosquittoBuilder::Build()
{
    return MosquittoContainer(BuildContainer());
}

WslContainerBuilder& MosquittoBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image ships /mosquitto/config/mosquitto.conf (listener 1883, allow_anonymous true) and an
    // ENTRYPOINT that execs its CMD, so only the CMD is supplied here.
    return builder.WithCommand("/usr/sbin/mosquitto", {"-c", "/mosquitto/config/mosquitto.conf"});
}

MosquittoContainer::MosquittoContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string MosquittoContainer::GetConnectionString() const
{
    return GetConnectionString(GetConnectEndpoint(DefaultPort));
}

std::string MosquittoContainer::GetConnectionString(WslEndpoint endpoint) const
{
    return "mqtt://" + FormatEndpoint(endpoint);
}

} // namespace wslc::modules
