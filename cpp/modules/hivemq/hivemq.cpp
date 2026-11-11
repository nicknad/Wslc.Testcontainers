#include "wslc/modules/hivemq.hpp"

#include <utility>

namespace wslc::modules
{

HiveMqBuilder::HiveMqBuilder()
    // Docker Hub publishes the CE Image under calendar versions. 2026.5 is a real tag, and the
    // repository only accepts immutable digits-first tags.
    : WslModuleBuilder<HiveMqBuilder>("docker.io/hivemq/hivemq-ce:2026.5", HiveMqContainer::DefaultPort,
                                      // HiveMQ logs "Started HiveMQ in <n>ms" after its listener accepts
                                      // clients. The version-independent prefix is the readiness token:
                                      // the earlier "Starting HiveMQ" line does not contain it, so it
                                      // cannot pass before the broker is ready.
                                      "Started HiveMQ")
{
}

HiveMqContainer HiveMqBuilder::Build()
{
    return HiveMqContainer(BuildContainer());
}

WslContainerBuilder& HiveMqBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image's /opt/docker-entrypoint.sh deletes the bundled allow-all extension unless
    // HIVEMQ_ALLOW_ALL_CLIENTS is "true"; the Image defaults it to true, and setting it keeps
    // anonymous access explicit if that default ever changes. WSLC never runs an Image
    // ENTRYPOINT/CMD, so the preset runs the entrypoint itself (same shape as the RabbitMQ module)
    // followed by the HiveMQ start script, which is what applies the switch before the broker starts.
    return builder.WithEnvironment("HIVEMQ_ALLOW_ALL_CLIENTS", "true")
        .WithCommand("/opt/docker-entrypoint.sh", {"/opt/hivemq/bin/run.sh"});
}

HiveMqContainer::HiveMqContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string HiveMqContainer::GetConnectionString() const
{
    return GetConnectionString(GetConnectEndpoint(DefaultPort));
}

std::string HiveMqContainer::GetConnectionString(WslEndpoint endpoint) const
{
    return "mqtt://" + FormatEndpoint(endpoint);
}

} // namespace wslc::modules
