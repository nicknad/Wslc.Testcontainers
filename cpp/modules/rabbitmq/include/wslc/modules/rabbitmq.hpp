#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class RabbitMqContainer;

/// <summary>
/// Testcontainers-style builder for RabbitMQ. Encapsulates the Image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// the credentials for anything shared.
/// </summary>
class RabbitMqBuilder : public WslModuleBuilder<RabbitMqBuilder>
{
public:
    /// <summary>Initializes the builder with the default RabbitMQ Image and readiness preset.</summary>
    RabbitMqBuilder();

    /// <summary>Sets the RABBITMQ_DEFAULT_USER value. Defaults to "rabbit".</summary>
    RabbitMqBuilder& WithUsername(std::string username);

    /// <summary>Sets the RABBITMQ_DEFAULT_PASS value. Defaults to "secret"; override for anything shared.</summary>
    RabbitMqBuilder& WithPassword(std::string password);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    RabbitMqContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

private:
    std::string m_username = "rabbit";
    std::string m_password = "secret";
};

/// <summary>
/// Typed RabbitMQ container: renders an AMQP 0-9-1 connection URI from the dynamic Host port
/// mapping.
/// </summary>
class RabbitMqContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port RabbitMQ speaks AMQP 0-9-1 on by default (5672).</summary>
    static constexpr int DefaultPort = 5672;

    RabbitMqContainer(const RabbitMqContainer&) = delete;
    RabbitMqContainer& operator=(const RabbitMqContainer&) = delete;
    RabbitMqContainer(RabbitMqContainer&&) noexcept = default;
    RabbitMqContainer& operator=(RabbitMqContainer&&) noexcept = default;

    /// <summary>
    /// Renders an AMQP 0-9-1 connection URI for the running container, e.g.
    /// amqp://rabbit:secret@127.0.0.1:49153/. Credentials are percent-encoded.
    /// </summary>
    std::string GetConnectionString() const;

private:
    friend class RabbitMqBuilder;
    RabbitMqContainer(WslContainer inner, std::string username, std::string password);

    std::string m_username;
    std::string m_password;
};

} // namespace wslc::modules
