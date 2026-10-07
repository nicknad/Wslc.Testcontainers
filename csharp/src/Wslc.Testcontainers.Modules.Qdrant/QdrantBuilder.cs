using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.Qdrant;

/// <summary>
/// Testcontainers-style builder for Qdrant. Encapsulates the image, ports and readiness waits
/// so tests don't memorize them. The HTTP API port is the module's primary port; the gRPC port
/// is mapped as well.
/// </summary>
public sealed class QdrantBuilder : WslModuleBuilder<QdrantBuilder>
{
    /// <summary>Initializes the builder with the default Qdrant image and readiness preset.</summary>
    public QdrantBuilder()
        : base(
            "docker.io/qdrant/qdrant:v1.19.2",
            QdrantContainer.HttpPort,
            string.Empty)
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public QdrantContainer Build() => new(BuildContainer());

    // Readiness is the /readyz HTTP check added in Configure instead of a log-message preset.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image has no ENTRYPOINT; its CMD is ./entrypoint.sh, which starts ./qdrant
            // from the /qdrant working directory.
            .WithCommand("./entrypoint.sh")
            .WithWorkingDirectory("/qdrant")
            .WithPort(QdrantContainer.GrpcPort)
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/readyz", QdrantContainer.HttpPort));
}
