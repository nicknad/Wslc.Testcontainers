namespace Wslc.Testcontainers.Modules.Redis;

/// <summary>
/// Typed Redis container: a module wrapper that renders a StackExchange.Redis-style endpoint
/// from the dynamic host port mapping.
/// </summary>
public sealed class RedisContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Redis listens on by default (6379).</summary>
    public const int DefaultPort = 6379;

    internal RedisContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the <c>host:port</c> endpoint for StackExchange.Redis (e.g. <c>127.0.0.1:49153</c>).</summary>
    public string GetEndpoint()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"{endpoint.Address}:{endpoint.Port}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
