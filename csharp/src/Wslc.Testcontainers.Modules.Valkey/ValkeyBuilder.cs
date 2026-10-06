namespace Wslc.Testcontainers.Modules.Valkey;

/// <summary>
/// Testcontainers-style builder for Valkey. Same module pattern as Redis: image, port and
/// readiness live in the module builder base. The builder is mutable; every <c>With...</c>
/// mutates and returns it for chaining.
/// </summary>
public sealed class ValkeyBuilder : WslModuleBuilder<ValkeyBuilder>
{
    /// <summary>Initializes the builder with the default Valkey image and readiness preset.</summary>
    public ValkeyBuilder()
        : base(
            "docker.io/valkey/valkey:8-alpine",
            ValkeyContainer.DefaultPort,
            "Ready to accept connections")
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public ValkeyContainer Build() => new(BuildContainer());

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh valkey-server).
        builder.WithCommand("/usr/local/bin/docker-entrypoint.sh", "valkey-server");
}
