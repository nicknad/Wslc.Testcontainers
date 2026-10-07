#include "wslc/modules/qdrant.hpp"

#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

QdrantBuilder::QdrantBuilder()
    : WslModuleBuilder<QdrantBuilder>("docker.io/qdrant/qdrant:v1.19.2", QdrantContainer::HttpPort, "")
{
}

QdrantContainer QdrantBuilder::Build()
{
    return QdrantContainer(BuildContainer());
}

WslContainerBuilder& QdrantBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image has no ENTRYPOINT; its CMD is ./entrypoint.sh, which starts ./qdrant from the
    // /qdrant working directory.
    return builder.WithCommand("./entrypoint.sh")
        .WithWorkingDirectory("/qdrant")
        .WithPort(QdrantContainer::GrpcPort)
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/readyz", QdrantContainer::HttpPort));
}

QdrantContainer::QdrantContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string QdrantContainer::GetEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(HttpPort);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
