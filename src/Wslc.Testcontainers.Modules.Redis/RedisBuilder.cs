namespace Wslc.Testcontainers.Modules.Redis;

// Testcontainers-style builder for Redis. Same module pattern as Postgres:
// image, port, and readiness live in the module builder base. The builder is
// mutable; every With... mutates and returns it for chaining.
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

    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh redis-server).
        builder.WithCommand("/usr/local/bin/docker-entrypoint.sh", "redis-server");
}
