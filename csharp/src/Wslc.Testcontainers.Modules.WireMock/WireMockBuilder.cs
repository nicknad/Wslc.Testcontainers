using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.WireMock;

/// <summary>
/// Testcontainers-style builder for WireMock. Encapsulates the image, port and readiness wait so
/// tests don't memorize them.
/// </summary>
public sealed class WireMockBuilder : WslModuleBuilder<WireMockBuilder>
{
    /// <summary>Initializes the builder with the default WireMock image and readiness preset.</summary>
    public WireMockBuilder()
        : base(
            "docker.io/wiremock/wiremock:3x",
            WireMockContainer.DefaultPort,
            string.Empty)
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public WireMockContainer Build() => new(BuildContainer());

    // WireMock's startup log lines are not reliably captured before the HTTP server is up, so
    // readiness is the /__admin/health HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image entrypoint prepends the WireMock Java command when the configured command
            // starts with a flag, so --verbose enables verbose logging.
            .WithCommand("--verbose")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/__admin/health", WireMockContainer.DefaultPort));
}
