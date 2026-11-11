#pragma once

#include "wslc/endpoint.hpp"
#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class MosquittoContainer;

/// <summary>
/// Testcontainers-style builder for Eclipse Mosquitto. Encapsulates the Image, port and
/// readiness waits so tests don't memorize them. The Image already ships a configuration that
/// listens on 1883 and allows anonymous clients, so the preset adds none.
/// </summary>
class MosquittoBuilder : public WslModuleBuilder<MosquittoBuilder>
{
public:
    /// <summary>Initializes the builder with the default Mosquitto Image and readiness preset.</summary>
    MosquittoBuilder();

    /// <summary>Builds the container with the Image's service command. The container is not started.</summary>
    MosquittoContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;
};

/// <summary>
/// Typed Eclipse Mosquitto container: a module wrapper that renders an MQTT URL from the dynamic
/// Host port mapping.
/// </summary>
class MosquittoContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Mosquitto accepts MQTT client connections on by default (1883).</summary>
    static constexpr int DefaultPort = 1883;

    MosquittoContainer(const MosquittoContainer&) = delete;
    MosquittoContainer& operator=(const MosquittoContainer&) = delete;
    MosquittoContainer(MosquittoContainer&&) noexcept = default;
    MosquittoContainer& operator=(MosquittoContainer&&) noexcept = default;

    /// <summary>
    /// Renders an MQTT URL for the running container, e.g. mqtt://127.0.0.1:49153.
    /// </summary>
    std::string GetConnectionString() const;

    /// <summary>
    /// Renders an MQTT URL for a caller-supplied endpoint, e.g. an address in front of the
    /// container such as a fault-relay listener. IPv6 hosts are bracketed.
    /// </summary>
    std::string GetConnectionString(WslEndpoint endpoint) const;

private:
    friend class MosquittoBuilder;
    explicit MosquittoContainer(WslContainer inner);
};

} // namespace wslc::modules
