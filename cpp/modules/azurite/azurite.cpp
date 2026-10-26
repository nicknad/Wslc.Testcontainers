#include "wslc/modules/azurite.hpp"

#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

AzuriteBuilder::AzuriteBuilder()
    : WslModuleBuilder<AzuriteBuilder>("mcr.microsoft.com/azure-storage/azurite:3.37.0", AzuriteContainer::BlobPort,
                                       "successfully listening")
{
}

AzuriteContainer AzuriteBuilder::Build()
{
    return AzuriteContainer(BuildContainer());
}

WslContainerBuilder& AzuriteBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image has no ENTRYPOINT; its CMD starts all three services. Bind every Host to 0.0.0.0
    // so the mapped Windows ports reach them, and opt out of telemetry.
    return builder
        .WithCommand("azurite", {"-l", "/data", "--blobHost", "0.0.0.0", "--queueHost", "0.0.0.0", "--tableHost",
                                 "0.0.0.0", "--skipApiVersionCheck", "--disableTelemetry"})
        .WithPort(AzuriteContainer::QueuePort)
        .WithPort(AzuriteContainer::TablePort);
}

AzuriteContainer::AzuriteContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string AzuriteContainer::GetBlobEndpoint() const
{
    return FormatServiceEndpoint(BlobPort);
}

std::string AzuriteContainer::GetQueueEndpoint() const
{
    return FormatServiceEndpoint(QueuePort);
}

std::string AzuriteContainer::GetTableEndpoint() const
{
    return FormatServiceEndpoint(TablePort);
}

std::string AzuriteContainer::GetConnectionString() const
{
    return "DefaultEndpointsProtocol=http;AccountName=" + std::string(AccountName) +
           ";AccountKey=" + std::string(AccountKey) + ";BlobEndpoint=" + GetBlobEndpoint() +
           ";QueueEndpoint=" + GetQueueEndpoint() + ";TableEndpoint=" + GetTableEndpoint() + ";";
}

std::string AzuriteContainer::FormatServiceEndpoint(int port) const
{
    return FormatHttpEndpoint(GetConnectEndpoint(port)) + "/" + AccountName;
}

} // namespace wslc::modules
