using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.RustFs;

/// <summary>
/// Testcontainers-style builder for RustFS (S3-compatible object storage). Encapsulates the
/// image, credentials, ports and readiness waits so tests don't memorize them. Defaults are the
/// image's local-test credentials; override <see cref="WithAccessKey"/>/<see cref="WithSecretKey"/>
/// for anything shared.
/// </summary>
public sealed class RustFsBuilder : WslModuleBuilder<RustFsBuilder>
{
    private string _accessKey = "rustfsadmin";
    private string _secretKey = "rustfsadmin";

    /// <summary>Initializes the builder with the default RustFS image and readiness preset.</summary>
    public RustFsBuilder()
        : base(
            "docker.io/rustfs/rustfs:1.0.1",
            RustFsContainer.S3Port,
            string.Empty)
    {
    }

    /// <summary>Sets the <c>RUSTFS_ACCESS_KEY</c> value. Defaults to <c>rustfsadmin</c>.</summary>
    public RustFsBuilder WithAccessKey(string accessKey)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(accessKey);
        _accessKey = accessKey;
        return this;
    }

    /// <summary>Sets the <c>RUSTFS_SECRET_KEY</c> value. Defaults to <c>rustfsadmin</c>; override for anything shared.</summary>
    public RustFsBuilder WithSecretKey(string secretKey)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(secretKey);
        _secretKey = secretKey;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public RustFsContainer Build() => new(BuildContainer(), _accessKey, _secretKey);

    // RustFS logs to an observability pipeline rather than a stable stdout readiness line, so
    // readiness is the /health HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            .WithEnvironment("RUSTFS_ACCESS_KEY", _accessKey)
            .WithEnvironment("RUSTFS_SECRET_KEY", _secretKey)
            .WithEnvironment("RUSTFS_ADDRESS", ":9000")
            .WithEnvironment("RUSTFS_CONSOLE_ADDRESS", ":9001")
            .WithEnvironment("RUSTFS_CONSOLE_ENABLE", "true")
            // The image entrypoint maps the configured command to its CMD: rustfs /data.
            .WithCommand("/data")
            .WithPort(RustFsContainer.ConsolePort)
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/health", RustFsContainer.S3Port));
}
