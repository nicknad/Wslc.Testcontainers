#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class QdrantContainer;

/// <summary>
/// Testcontainers-style builder for Qdrant. Encapsulates the Image, ports and readiness waits
/// so tests don't memorize them. The HTTP API port is the module's primary port; the gRPC port
/// is mapped as well.
/// </summary>
class QdrantBuilder : public WslModuleBuilder<QdrantBuilder>
{
public:
    /// <summary>Initializes the builder with the default Qdrant Image and readiness preset.</summary>
    QdrantBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    QdrantContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Readiness is the /readyz HTTP check added in Configure instead of a log-message preset.
    int ReadyMessageOccurrences() const override { return 0; }
};

/// <summary>
/// Typed Qdrant container: renders the HTTP API endpoint from the dynamic Host port mapping.
/// </summary>
class QdrantContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Qdrant serves its HTTP API on by default (6333).</summary>
    static constexpr int HttpPort = 6333;

    /// <summary>The Linux TCP port Qdrant serves its gRPC API on by default (6334).</summary>
    static constexpr int GrpcPort = 6334;

    QdrantContainer(const QdrantContainer&) = delete;
    QdrantContainer& operator=(const QdrantContainer&) = delete;
    QdrantContainer(QdrantContainer&&) noexcept = default;
    QdrantContainer& operator=(QdrantContainer&&) noexcept = default;

    /// <summary>Renders the Qdrant HTTP API base URL (e.g. http://127.0.0.1:49153).</summary>
    std::string GetEndpoint() const;

private:
    friend class QdrantBuilder;
    explicit QdrantContainer(WslContainer inner);
};

} // namespace wslc::modules
