#include "wslc/modules/redis.hpp"

#include <utility>

namespace wslc::modules
{

RedisBuilder::RedisBuilder()
    : WslModuleBuilder<RedisBuilder>("docker.io/library/redis:7-alpine", RedisContainer::ContainerPort,
                                     "Ready to accept connections")
{
}

RedisContainer RedisBuilder::Build()
{
    return RedisContainer(BuildContainer());
}

WslContainerBuilder& RedisBuilder::Configure(WslContainerBuilder& builder)
{
    // Replicates the Image ENTRYPOINT/CMD (docker-entrypoint.sh redis-server).
    return builder.WithCommand("/usr/local/bin/docker-entrypoint.sh", {"redis-server"});
}

RedisContainer::RedisContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string RedisContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(ContainerPort);
    return endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
