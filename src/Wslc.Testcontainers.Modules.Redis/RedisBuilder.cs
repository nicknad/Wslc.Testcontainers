namespace Wslc.Testcontainers.Modules.Redis;

// Testcontainers-style builder for Redis. Same module pattern as Postgres:
// image, port, and readiness live in the module builder base.
public sealed class RedisBuilder : WslModuleBuilder<RedisBuilder>
{
    public RedisBuilder()
        : base(
            "docker.io/library/redis:7-alpine",
            RedisContainer.ContainerPort,
            "Ready to accept connections")
    {
    }

    public RedisContainer Build() => new(BuildContainer());
}
