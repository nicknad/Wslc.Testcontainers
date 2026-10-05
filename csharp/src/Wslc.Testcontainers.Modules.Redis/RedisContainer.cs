namespace Wslc.Testcontainers.Modules.Redis;

/// <summary>
/// Typed Redis container: a module wrapper that renders a StackExchange.Redis-style endpoint
/// from the dynamic host port mapping.
/// </summary>
public sealed class RedisContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Redis listens on (6379).</summary>
    public const int ContainerPort = 6379;

    internal RedisContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Gets the Windows port mapped to <see cref="ContainerPort"/>.</summary>
    public int GetMappedPort() => GetConnectEndpoint(ContainerPort).Port;

    /// <summary>Renders the <c>host:port</c> endpoint for StackExchange.Redis.</summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(ContainerPort);
        return $"{endpoint.Address}:{endpoint.Port}";
    }
}
