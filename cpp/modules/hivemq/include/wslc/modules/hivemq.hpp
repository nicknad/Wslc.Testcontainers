#pragma once

#include "wslc/endpoint.hpp"
#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class HiveMqContainer;

/// <summary>
/// Testcontainers-style builder for HiveMQ Community Edition. Encapsulates the Image, port and
/// readiness waits so tests don't memorize them. The Image enables its bundled allow-all extension
/// by default, so anonymous MQTT clients can connect; the preset sets that switch explicitly so a
/// later Image default cannot silently require credentials.
/// </summary>
class HiveMqBuilder : public WslModuleBuilder<HiveMqBuilder>
{
public:
    /// <summary>Initializes the builder with the default HiveMQ CE Image and readiness preset.</summary>
    HiveMqBuilder();

    /// <summary>Builds the container with the Image's service command. The container is not started.</summary>
    HiveMqContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;
};

/// <summary>
/// Typed HiveMQ Community Edition container: a module wrapper that renders an MQTT URL from the
/// dynamic Host port mapping.
/// </summary>
class HiveMqContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port HiveMQ accepts MQTT client connections on by default (1883).</summary>
    static constexpr int DefaultPort = 1883;

    HiveMqContainer(const HiveMqContainer&) = delete;
    HiveMqContainer& operator=(const HiveMqContainer&) = delete;
    HiveMqContainer(HiveMqContainer&&) noexcept = default;
    HiveMqContainer& operator=(HiveMqContainer&&) noexcept = default;

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
    friend class HiveMqBuilder;
    explicit HiveMqContainer(WslContainer inner);
};

} // namespace wslc::modules
