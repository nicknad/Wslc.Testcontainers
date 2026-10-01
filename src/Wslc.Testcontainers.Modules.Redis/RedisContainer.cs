namespace Wslc.Testcontainers.Modules.Redis;

// Typed wrapper for Redis. Renders a StackExchange.Redis-style endpoint from
// the dynamic host port mapping.
public sealed class RedisContainer : WslModuleContainer
{
    public const int ContainerPort = 6379;

    internal RedisContainer(IWslContainer inner)
        : base(inner)
    {
    }

    public int GetMappedPort() => base.GetMappedPort(ContainerPort);

    public string GetConnectionString() => $"{Host}:{GetMappedPort()}";
}
