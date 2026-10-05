#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class RedisContainer;

/// <summary>
/// Testcontainers-style builder for Redis. Same module pattern as Postgres: Image, port and
/// readiness live in the module builder base.
/// </summary>
class RedisBuilder : public WslModuleBuilder<RedisBuilder>
{
public:
    /// <summary>Initializes the builder with the default Redis Image and readiness preset.</summary>
    RedisBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    RedisContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;
};

/// <summary>
/// Typed Redis container: renders a StackExchange.Redis-style endpoint from the dynamic Host
/// port mapping.
/// </summary>
class RedisContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Redis listens on by default (6379).</summary>
    static constexpr int DefaultPort = 6379;

    RedisContainer(const RedisContainer&) = delete;
    RedisContainer& operator=(const RedisContainer&) = delete;
    RedisContainer(RedisContainer&&) noexcept = default;
    RedisContainer& operator=(RedisContainer&&) noexcept = default;

    /// <summary>Renders the host:port endpoint for StackExchange.Redis (e.g. 127.0.0.1:49153).</summary>
    std::string GetEndpoint() const;

private:
    friend class RedisBuilder;
    explicit RedisContainer(WslContainer inner);
};

} // namespace wslc::modules
