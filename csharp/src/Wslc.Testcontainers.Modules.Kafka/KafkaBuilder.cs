using System.Globalization;
using System.Net;
using System.Net.Sockets;

namespace Wslc.Testcontainers.Modules.Kafka;

/// <summary>
/// Testcontainers-style builder for a single-node Apache Kafka broker in KRaft mode. Encapsulates
/// the image, the KRaft environment, the advertised listener and the readiness waits so tests do
/// not memorize them.
/// </summary>
/// <remarks>
/// Kafka redirects clients from the bootstrap address to its advertised listener, so the host port
/// must be known before the broker starts. This builder reserves a fixed loopback host port instead
/// of a dynamic one; if that port is taken by the time the container starts, <c>StartAsync</c> fails
/// rather than silently picking another port. Avoid starting several Kafka containers in parallel.
/// </remarks>
public sealed class KafkaBuilder : WslModuleBuilder<KafkaBuilder>
{
    private int _hostPort;

    /// <summary>Initializes the builder with the default Apache Kafka image and readiness preset.</summary>
    public KafkaBuilder()
        : base(
            "docker.io/apache/kafka:4.3.1",
            KafkaContainer.DefaultPort,
            "Kafka Server started")
    {
    }

    /// <summary>Builds the broker with a reserved host port. The container is not started.</summary>
    public KafkaContainer Build()
    {
        _hostPort = ReserveHostPort();
        return new KafkaContainer(BuildContainer());
    }

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        return builder
            // Pin the exact host port so the broker can advertise the address clients reach it on.
            .WithPort(KafkaContainer.DefaultPort, _hostPort)
            // Single-node KRaft: this process is both broker and controller, so no ZooKeeper.
            .WithEnvironment("KAFKA_NODE_ID", "1")
            .WithEnvironment("KAFKA_PROCESS_ROLES", "broker,controller")
            .WithEnvironment("KAFKA_LISTENERS", "PLAINTEXT://:9092,CONTROLLER://:9093")
            .WithEnvironment(
                "KAFKA_ADVERTISED_LISTENERS",
                "PLAINTEXT://127.0.0.1:" + _hostPort.ToString(CultureInfo.InvariantCulture))
            .WithEnvironment("KAFKA_CONTROLLER_LISTENER_NAMES", "CONTROLLER")
            .WithEnvironment("KAFKA_LISTENER_SECURITY_PROTOCOL_MAP", "CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT")
            .WithEnvironment("KAFKA_CONTROLLER_QUORUM_VOTERS", "1@127.0.0.1:9093")
            .WithEnvironment("KAFKA_INTER_BROKER_LISTENER_NAME", "PLAINTEXT")
            .WithEnvironment("KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR", "1")
            .WithEnvironment("KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR", "1")
            .WithEnvironment("KAFKA_TRANSACTION_STATE_LOG_MIN_ISR", "1")
            .WithEnvironment("KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS", "0")
            // The image ENTRYPOINT renders the KRaft config from the KAFKA_* variables and starts
            // the broker. WSLC never runs it automatically, so declare it explicitly.
            .WithCommand("/etc/kafka/docker/run");
    }

    // Reserve a fixed loopback port by binding port 0 and releasing it. The runtime binds the
    // exact port when the container starts and fails fast if it has been taken meanwhile.
    private static int ReserveHostPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            return ((IPEndPoint)listener.LocalEndpoint).Port;
        }
        finally
        {
            listener.Stop();
        }
    }
}
