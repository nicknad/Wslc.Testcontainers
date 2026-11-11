using System.Net;

namespace Wslc.Testcontainers.Modules.Artemis;

/// <summary>
/// Typed ActiveMQ Artemis container: a module wrapper that renders an MQTT URL from the dynamic
/// host port mapping.
/// </summary>
public sealed class ArtemisContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Artemis accepts MQTT client connections on by default (1883).</summary>
    public const int DefaultPort = 1883;

    private readonly string? _username;
    private readonly string? _password;

    internal ArtemisContainer(IWslContainer inner, string? username, string? password)
        : base(inner)
    {
        _username = username;
        _password = password;
    }

    /// <summary>
    /// Renders an MQTT URL for the running container, e.g. <c>mqtt://127.0.0.1:49153</c> for the
    /// default anonymous access, or <c>mqtt://user:pass@127.0.0.1:49153</c> when
    /// <see cref="ArtemisBuilder.WithCredentials"/> configured credentials.
    /// </summary>
    public string GetConnectionString() => GetConnectionString(GetConnectEndpoint(DefaultPort));

    // Deliberately an instance member: callers hold a container and reach both the container
    // endpoint and a relay endpoint through the same receiver and method name. CA1822 would
    // make this static, which forces a different call shape for the same concept.
#pragma warning disable CA1822
    /// <summary>
    /// Renders an MQTT URL for a caller-supplied endpoint, e.g. an address in front of the
    /// container such as a fault-relay listener. IPv6 hosts are bracketed. Credentials are
    /// percent-encoded when configured.
    /// </summary>
    public string GetConnectionString(IPEndPoint endpoint)
#pragma warning restore CA1822
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        if (_username is null)
        {
            return $"mqtt://{FormatEndpoint(endpoint)}";
        }

        var user = Uri.EscapeDataString(_username);
        var password = Uri.EscapeDataString(_password!);
        return $"mqtt://{user}:{password}@{FormatEndpoint(endpoint)}";
    }
}
