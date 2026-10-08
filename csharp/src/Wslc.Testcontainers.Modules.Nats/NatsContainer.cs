namespace Wslc.Testcontainers.Modules.Nats;

/// <summary>
/// Typed NATS container: a module wrapper that renders a NATS client connection URL from the
/// dynamic host port mapping.
/// </summary>
public sealed class NatsContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port NATS accepts client connections on by default (4222).</summary>
    public const int DefaultPort = 4222;

    private readonly string? _username;
    private readonly string? _password;

    internal NatsContainer(IWslContainer inner, string? username, string? password)
        : base(inner)
    {
        _username = username;
        _password = password;
    }

    /// <summary>
    /// Renders a NATS client URL for the running container, e.g. <c>nats://127.0.0.1:49153</c>
    /// or, with credentials, <c>nats://user:password@127.0.0.1:49153</c>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        if (_username is null)
        {
            return $"nats://{FormatEndpoint(endpoint)}";
        }

        var user = Uri.EscapeDataString(_username);
        var password = Uri.EscapeDataString(_password!);
        return $"nats://{user}:{password}@{FormatEndpoint(endpoint)}";
    }
}
