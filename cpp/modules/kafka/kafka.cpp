#include "wslc/modules/kafka.hpp"

#include "wslc/exceptions.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <utility>

namespace wslc::modules
{

namespace
{

/// Reserves a fixed loopback port by binding port 0 and releasing it. The runtime binds the exact
/// port when the container starts and fails fast if it has been taken meanwhile.
int ReserveHostPort()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        throw WslException("Failed to initialize Winsock while reserving a Kafka host port.");
    }

    const SOCKET handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == INVALID_SOCKET)
    {
        WSACleanup();
        throw WslException("Failed to create a socket while reserving a Kafka host port.");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    int port = 0;
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
    {
        int length = sizeof(address);
        if (getsockname(handle, reinterpret_cast<sockaddr*>(&address), &length) == 0)
        {
            port = ntohs(address.sin_port);
        }
    }

    closesocket(handle);
    WSACleanup();

    if (port == 0)
    {
        throw WslException("Failed to reserve a Kafka host port.");
    }

    return port;
}

} // namespace

KafkaBuilder::KafkaBuilder()
    : WslModuleBuilder<KafkaBuilder>("docker.io/apache/kafka:4.3.1", KafkaContainer::DefaultPort,
                                     "Kafka Server started")
{
}

KafkaContainer KafkaBuilder::Build()
{
    m_hostPort = ReserveHostPort();
    return KafkaContainer(BuildContainer());
}

WslContainerBuilder& KafkaBuilder::Configure(WslContainerBuilder& builder)
{
    return builder
        // Pin the exact host port so the broker can advertise the address clients reach it on.
        .WithPort(KafkaContainer::DefaultPort, m_hostPort)
        // Single-node KRaft: this process is both broker and controller, so no ZooKeeper.
        .WithEnvironment("KAFKA_NODE_ID", "1")
        .WithEnvironment("KAFKA_PROCESS_ROLES", "broker,controller")
        .WithEnvironment("KAFKA_LISTENERS", "PLAINTEXT://:9092,CONTROLLER://:9093")
        .WithEnvironment("KAFKA_ADVERTISED_LISTENERS", "PLAINTEXT://127.0.0.1:" + std::to_string(m_hostPort))
        .WithEnvironment("KAFKA_CONTROLLER_LISTENER_NAMES", "CONTROLLER")
        .WithEnvironment("KAFKA_LISTENER_SECURITY_PROTOCOL_MAP", "CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT")
        .WithEnvironment("KAFKA_CONTROLLER_QUORUM_VOTERS", "1@127.0.0.1:9093")
        .WithEnvironment("KAFKA_INTER_BROKER_LISTENER_NAME", "PLAINTEXT")
        .WithEnvironment("KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR", "1")
        .WithEnvironment("KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR", "1")
        .WithEnvironment("KAFKA_TRANSACTION_STATE_LOG_MIN_ISR", "1")
        .WithEnvironment("KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS", "0")
        // The image ENTRYPOINT renders the KRaft config from the KAFKA_* variables and starts the
        // broker. WSLC never runs it automatically, so declare it explicitly.
        .WithCommand("/etc/kafka/docker/run");
}

KafkaContainer::KafkaContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string KafkaContainer::GetBootstrapServers() const
{
    return FormatEndpoint(GetConnectEndpoint(DefaultPort));
}

} // namespace wslc::modules
