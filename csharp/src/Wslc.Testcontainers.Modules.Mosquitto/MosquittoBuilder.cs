namespace Wslc.Testcontainers.Modules.Mosquitto;

/// <summary>
/// Testcontainers-style builder for Eclipse Mosquitto. Encapsulates the image, port and readiness
/// waits so tests don't memorize them. The image already ships a configuration that listens on
/// 1883 and allows anonymous clients, so the preset adds none.
/// </summary>
public sealed class MosquittoBuilder : WslModuleBuilder<MosquittoBuilder>
{
    /// <summary>Initializes the builder with the default Mosquitto image and readiness preset.</summary>
    public MosquittoBuilder()
        : base(
            "docker.io/library/eclipse-mosquitto:2.1.2",
            MosquittoContainer.DefaultPort,
            // Mosquitto logs "mosquitto version <version> running" once its listener sockets are
            // open. The version-independent tail is the readiness token: the earlier
            // "starting" line does not contain it, so it cannot pass before the broker accepts
            // clients. Confirm the token against real logs on the first integration run.
            "running")
    {
    }

    /// <summary>Builds the container with the image's service command. The container is not started.</summary>
    public MosquittoContainer Build() => new(BuildContainer());

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        // The image ships /mosquitto/config/mosquitto.conf (listener 1883, allow_anonymous true)
        // and an ENTRYPOINT that execs its CMD, so only the CMD is supplied here.
        builder.WithCommand("/usr/sbin/mosquitto", "-c", "/mosquitto/config/mosquitto.conf");
}
