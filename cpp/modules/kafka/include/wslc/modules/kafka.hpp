#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class KafkaContainer;

/// <summary>
/// Testcontainers-style builder for a single-node Apache Kafka broker in KRaft mode. Encapsulates
/// the Image, the KRaft environment, the advertised listener and the readiness waits so tests do
/// not memorize them.
///
/// Kafka redirects clients from the bootstrap address to its advertised listener, so the host port
/// must be known before the broker starts. This builder reserves a fixed loopback host port instead
/// of a dynamic one; if that port is taken by the time the container starts, Start fails rather than
/// silently picking another port. Avoid starting several Kafka containers in parallel.
/// </summary>
class KafkaBuilder : public WslModuleBuilder<KafkaBuilder>
{
public:
    /// <summary>Initializes the builder with the default Apache Kafka Image and readiness preset.</summary>
    KafkaBuilder();

    /// <summary>Builds the broker with a reserved host port. The container is not started.</summary>
    KafkaContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

private:
    int m_hostPort = 0;
};

/// <summary>
/// Typed single-node Kafka container: renders the bootstrap servers string from the fixed host
/// port it advertises.
/// </summary>
class KafkaContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Kafka accepts client connections on by default (9092).</summary>
    static constexpr int DefaultPort = 9092;

    KafkaContainer(const KafkaContainer&) = delete;
    KafkaContainer& operator=(const KafkaContainer&) = delete;
    KafkaContainer(KafkaContainer&&) noexcept = default;
    KafkaContainer& operator=(KafkaContainer&&) noexcept = default;

    /// <summary>
    /// Renders the bootstrap servers for the running broker, e.g. 127.0.0.1:49153. The same fixed
    /// host port is advertised to clients, so the address a client bootstraps with is also the one
    /// the broker returns while discovering the cluster.
    /// </summary>
    std::string GetBootstrapServers() const;

private:
    friend class KafkaBuilder;
    explicit KafkaContainer(WslContainer inner);
};

} // namespace wslc::modules
