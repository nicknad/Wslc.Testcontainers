#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class AzuriteContainer;

/// <summary>
/// Testcontainers-style builder for Azurite, Microsoft's local Azure Storage emulator.
/// Encapsulates the Image, the Blob/Queue/Table ports and the readiness waits so tests don't
/// memorize them.
/// </summary>
class AzuriteBuilder : public WslModuleBuilder<AzuriteBuilder>
{
public:
    /// <summary>Initializes the builder with the default Azurite Image and readiness preset.</summary>
    AzuriteBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    AzuriteContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Each service logs its own "successfully listening" line, so all three must appear before
    // the container is ready.
    int ReadyMessageOccurrences() const override { return 3; }
};

/// <summary>
/// Typed Azurite container: renders the Blob, Queue and Table endpoints and the development
/// connection string from the dynamic Host port mappings.
/// </summary>
class AzuriteContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Azurite's Blob service listens on by default (10000).</summary>
    static constexpr int BlobPort = 10000;

    /// <summary>The Linux TCP port Azurite's Queue service listens on by default (10001).</summary>
    static constexpr int QueuePort = 10001;

    /// <summary>The Linux TCP port Azurite's Table service listens on by default (10002).</summary>
    static constexpr int TablePort = 10002;

    /// <summary>The well-known Azurite development account name.</summary>
    static constexpr const char* AccountName = "devstoreaccount1";

    /// <summary>The public Azurite development account key. Not a secret; for test use only.</summary>
    static constexpr const char* AccountKey =
        "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";

    AzuriteContainer(const AzuriteContainer&) = delete;
    AzuriteContainer& operator=(const AzuriteContainer&) = delete;
    AzuriteContainer(AzuriteContainer&&) noexcept = default;
    AzuriteContainer& operator=(AzuriteContainer&&) noexcept = default;

    /// <summary>Renders the Blob service endpoint including the account path, e.g.
    /// http://127.0.0.1:49153/devstoreaccount1.</summary>
    std::string GetBlobEndpoint() const;

    /// <summary>Renders the Queue service endpoint including the account path, e.g.
    /// http://127.0.0.1:49154/devstoreaccount1.</summary>
    std::string GetQueueEndpoint() const;

    /// <summary>Renders the Table service endpoint including the account path, e.g.
    /// http://127.0.0.1:49155/devstoreaccount1.</summary>
    std::string GetTableEndpoint() const;

    /// <summary>Renders the development storage connection string for the running container, with
    /// explicit endpoints because the Windows Host ports are dynamic.</summary>
    std::string GetConnectionString() const;

private:
    friend class AzuriteBuilder;
    explicit AzuriteContainer(WslContainer inner);

    std::string FormatServiceEndpoint(int port) const;
};

} // namespace wslc::modules
