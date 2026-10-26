using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.Azurite;

/// <summary>
/// Testcontainers-style builder for Azurite, Microsoft's local Azure Storage emulator. Encapsulates
/// the image, the Blob/Queue/Table ports and the readiness waits so tests don't memorize them.
/// </summary>
public sealed class AzuriteBuilder : WslModuleBuilder<AzuriteBuilder>
{
    /// <summary>Initializes the builder with the default Azurite image and readiness preset.</summary>
    public AzuriteBuilder()
        : base(
            "mcr.microsoft.com/azure-storage/azurite:3.37.0",
            AzuriteContainer.BlobPort,
            "successfully listening")
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public AzuriteContainer Build() => new(BuildContainer());

    // Each service logs its own "successfully listening" line, so all three must appear before
    // the container is ready.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 3;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image has no ENTRYPOINT; its CMD starts all three services. Bind every host to
            // 0.0.0.0 so the mapped Windows ports reach them, and opt out of telemetry.
            .WithCommand(
                "azurite",
                "-l", "/data",
                "--blobHost", "0.0.0.0",
                "--queueHost", "0.0.0.0",
                "--tableHost", "0.0.0.0",
                "--skipApiVersionCheck",
                "--disableTelemetry")
            .WithPort(AzuriteContainer.QueuePort)
            .WithPort(AzuriteContainer.TablePort);
}
