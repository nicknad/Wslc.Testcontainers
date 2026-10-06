using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.MailPit;

/// <summary>
/// Testcontainers-style builder for Mailpit. Encapsulates the image, ports and readiness waits
/// so tests don't memorize them. The SMTP port is the module's primary port; the HTTP UI/API
/// port is mapped as well.
/// </summary>
public sealed class MailPitBuilder : WslModuleBuilder<MailPitBuilder>
{
    /// <summary>Initializes the builder with the default Mailpit image and readiness preset.</summary>
    public MailPitBuilder()
        : base(
            "docker.io/axllent/mailpit:v1.31",
            MailPitContainer.SmtpPort,
            string.Empty)
    {
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public MailPitContainer Build() => new(BuildContainer());

    // Mailpit's startup log lines are not reliably captured before the HTTP server is up, so
    // readiness is the /livez HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image entrypoint is the mailpit binary itself, so the configured command is
            // its CMD: a root flag starts the server (and keeps tests from phoning home).
            .WithCommand("--disable-version-check")
            .WithPort(MailPitContainer.HttpPort)
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/livez", MailPitContainer.HttpPort));
}
