namespace Wslc.Testcontainers.Modules.HiveMq;

/// <summary>
/// Testcontainers-style builder for HiveMQ Community Edition. Encapsulates the image, port and
/// readiness waits so tests don't memorize them. The image enables its bundled allow-all
/// extension by default, so anonymous MQTT clients can connect; the preset sets that switch
/// explicitly so a later image default cannot silently require credentials.
/// </summary>
public sealed class HiveMqBuilder : WslModuleBuilder<HiveMqBuilder>
{
    /// <summary>Initializes the builder with the default HiveMQ CE image and readiness preset.</summary>
    public HiveMqBuilder()
        : base(
            // Docker Hub publishes the CE image under calendar versions. 2026.5 is a real tag,
            // and the repository only accepts immutable digits-first tags.
            "docker.io/hivemq/hivemq-ce:2026.5",
            HiveMqContainer.DefaultPort,
            // HiveMQ logs "Started HiveMQ in <n>ms" after its listener accepts clients. The
            // version-independent prefix is the readiness token: the earlier "Starting HiveMQ"
            // line does not contain it, so it cannot pass before the broker is ready. Confirm the
            // token against real logs on the first integration run.
            "Started HiveMQ")
    {
    }

    /// <summary>Builds the container with the image's service command. The container is not started.</summary>
    public HiveMqContainer Build() => new(BuildContainer());

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image's /opt/docker-entrypoint.sh deletes the bundled allow-all extension
            // unless HIVEMQ_ALLOW_ALL_CLIENTS is "true"; the image defaults it to true, and
            // setting it keeps anonymous access explicit if that default ever changes.
            .WithEnvironment("HIVEMQ_ALLOW_ALL_CLIENTS", "true")
            // WSLC never runs an image ENTRYPOINT/CMD, so the preset runs the entrypoint itself
            // (same shape as the RabbitMQ module) followed by the HiveMQ start script. That is
            // what applies the switch above before the broker starts.
            .WithCommand("/opt/docker-entrypoint.sh", "/opt/hivemq/bin/run.sh");
}
