namespace Wslc.Testcontainers.Modules.Kafka;

/// <summary>
/// Typed single-node Kafka container: renders the bootstrap servers string from the fixed host
/// port it advertises.
/// </summary>
public sealed class KafkaContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Kafka accepts client connections on by default (9092).</summary>
    public const int DefaultPort = 9092;

    internal KafkaContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>
    /// Renders the bootstrap servers for the running broker, e.g. <c>127.0.0.1:49153</c>. The same
    /// fixed host port is advertised to clients, so the address a client bootstraps with is also
    /// the one the broker returns while discovering the cluster.
    /// </summary>
    public string GetBootstrapServers() => FormatEndpoint(GetConnectEndpoint(DefaultPort));
}
