namespace Wslc.Testcontainers.Modules.Redis;

/// <summary>
/// Testcontainers-style builder for Redis. Same module pattern as Postgres: image, port and
/// readiness live in the module builder base. The builder is mutable; every <c>With...</c>
/// mutates and returns it for chaining.
/// </summary>
public sealed class RedisBuilder : WslModuleBuilder<RedisBuilder>
{
    /// <summary>Initializes the builder with the default Redis image and readiness preset.</summary>
    public RedisBuilder()
        : base(
            "docker.io/library/redis:7-alpine",
            RedisContainer.DefaultPort,
            "Ready to accept connections")
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public RedisContainer Build() => new(BuildContainer());

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh redis-server).
        builder.WithCommand("/usr/local/bin/docker-entrypoint.sh", "redis-server");
}
