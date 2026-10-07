namespace Wslc.Testcontainers.Modules.RabbitMq;

/// <summary>
/// Typed RabbitMQ container: a module wrapper that renders an AMQP connection URI from the
/// dynamic host port mapping.
/// </summary>
public sealed class RabbitMqContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port RabbitMQ speaks AMQP 0-9-1 on by default (5672).</summary>
    public const int DefaultPort = 5672;

    private readonly string _username;
    private readonly string _password;

    internal RabbitMqContainer(IWslContainer inner, string username, string password)
        : base(inner)
    {
        _username = username;
        _password = password;
    }

    /// <summary>
    /// Renders an AMQP 0-9-1 connection URI for the running container, e.g.
    /// <c>amqp://rabbit:secret@127.0.0.1:49153/</c>. Credentials are percent-encoded.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        var user = Uri.EscapeDataString(_username);
        var password = Uri.EscapeDataString(_password);
        return $"amqp://{user}:{password}@{endpoint.Address}:{endpoint.Port}/";
    }
}
