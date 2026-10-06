#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class ValkeyContainer;

/// <summary>
/// Testcontainers-style builder for Valkey. Same module pattern as Redis: Image, port and
/// readiness live in the module builder base.
/// </summary>
class ValkeyBuilder : public WslModuleBuilder<ValkeyBuilder>
{
public:
    /// <summary>Initializes the builder with the default Valkey Image and readiness preset.</summary>
    ValkeyBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    ValkeyContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;
};

/// <summary>
/// Typed Valkey container: renders a StackExchange.Redis-style endpoint from the dynamic Host
/// port mapping.
/// </summary>
class ValkeyContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Valkey listens on by default (6379).</summary>
    static constexpr int DefaultPort = 6379;

    ValkeyContainer(const ValkeyContainer&) = delete;
    ValkeyContainer& operator=(const ValkeyContainer&) = delete;
    ValkeyContainer(ValkeyContainer&&) noexcept = default;
    ValkeyContainer& operator=(ValkeyContainer&&) noexcept = default;

    /// <summary>Renders the host:port endpoint for StackExchange.Redis (e.g. 127.0.0.1:49153).</summary>
    std::string GetEndpoint() const;

private:
    friend class ValkeyBuilder;
    explicit ValkeyContainer(WslContainer inner);
};

} // namespace wslc::modules
