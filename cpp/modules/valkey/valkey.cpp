#include "wslc/modules/valkey.hpp"

#include <utility>

namespace wslc::modules
{

ValkeyBuilder::ValkeyBuilder()
    : WslModuleBuilder<ValkeyBuilder>("docker.io/valkey/valkey:8-alpine", ValkeyContainer::DefaultPort,
                                      "Ready to accept connections")
{
}

ValkeyContainer ValkeyBuilder::Build()
{
    return ValkeyContainer(BuildContainer());
}

WslContainerBuilder& ValkeyBuilder::Configure(WslContainerBuilder& builder)
{
    // Replicates the Image ENTRYPOINT/CMD (docker-entrypoint.sh valkey-server).
    return builder.WithCommand("/usr/local/bin/docker-entrypoint.sh", {"valkey-server"});
}

ValkeyContainer::ValkeyContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string ValkeyContainer::GetEndpoint() const
{
    return FormatEndpoint(GetConnectEndpoint(DefaultPort));
}

} // namespace wslc::modules
