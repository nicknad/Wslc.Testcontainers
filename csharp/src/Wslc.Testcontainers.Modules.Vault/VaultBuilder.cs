using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.Vault;

/// <summary>
/// Testcontainers-style builder for HashiCorp Vault. Encapsulates the image, dev-mode command,
/// port and readiness waits so tests don't memorize them. The default runs Vault in dev mode
/// with a fixed root token; override <see cref="WithRootToken"/> when a test needs its own.
/// Dev mode stores everything in memory and is for tests only.
/// </summary>
public sealed class VaultBuilder : WslModuleBuilder<VaultBuilder>
{
    private string _rootToken = "root";

    /// <summary>Initializes the builder with the default Vault image and readiness preset.</summary>
    public VaultBuilder()
        : base(
            "docker.io/hashicorp/vault:2.1",
            VaultContainer.DefaultPort,
            string.Empty)
    {
    }

    /// <summary>Sets the dev-mode root token. Defaults to <c>root</c>.</summary>
    public VaultBuilder WithRootToken(string rootToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rootToken);
        _rootToken = rootToken;
        return this;
    }

    /// <summary>Builds the container with the configured root token. The container is not started.</summary>
    public VaultContainer Build() => new(BuildContainer(), _rootToken);

    // Vault's startup output has no stable single readiness line, so readiness is the
    // /v1/sys/health HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image entrypoint maps the configured command to its CMD: vault server.
            // Dev mode starts unsealed and ready, which is what tests need.
            .WithCommand(
                "server",
                "-dev",
                $"-dev-root-token-id={_rootToken}",
                $"-dev-listen-address=0.0.0.0:{VaultContainer.DefaultPort}")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/v1/sys/health", VaultContainer.DefaultPort));
}
