using System.Net;

namespace Wslc.Testcontainers.Modules.HiveMq;

/// <summary>
/// Typed HiveMQ Community Edition container: a module wrapper that renders an MQTT URL from the
/// dynamic host port mapping.
/// </summary>
public sealed class HiveMqContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port HiveMQ accepts MQTT client connections on by default (1883).</summary>
    public const int DefaultPort = 1883;

    internal HiveMqContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>
    /// Renders an MQTT URL for the running container, e.g. <c>mqtt://127.0.0.1:49153</c>.
    /// </summary>
    public string GetConnectionString() => GetConnectionString(GetConnectEndpoint(DefaultPort));

    // Deliberately an instance member: callers hold a container and reach both the container
    // endpoint and a relay endpoint through the same receiver and method name. CA1822 would
    // make this static, which forces a different call shape for the same concept.
#pragma warning disable CA1822
    /// <summary>
    /// Renders an MQTT URL for a caller-supplied endpoint, e.g. an address in front of the
    /// container such as a fault-relay listener. IPv6 hosts are bracketed.
    /// </summary>
    public string GetConnectionString(IPEndPoint endpoint)
#pragma warning restore CA1822
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        return $"mqtt://{FormatEndpoint(endpoint)}";
    }
}
