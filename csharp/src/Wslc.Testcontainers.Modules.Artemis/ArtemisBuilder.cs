namespace Wslc.Testcontainers.Modules.Artemis;

/// <summary>
/// Testcontainers-style builder for ActiveMQ Artemis. Encapsulates the image, port and readiness
/// waits so tests don't memorize them. The image creates its broker instance on first start and
/// the generated configuration exposes MQTT on 1883, so the preset supplies no <c>broker.xml</c>.
/// Anonymous clients are accepted by default; <see cref="WithCredentials"/> also creates a named
/// user for the URI.
/// </summary>
public sealed class ArtemisBuilder : WslModuleBuilder<ArtemisBuilder>
{
    // The image's docker-run.sh always calls artemis create with these values; anonymous clients
    // need none of them. They stay overridable through WithCredentials.
    private string _username = "artemis";
    private string _password = "artemis";
    private bool _credentialsInUri;

    /// <summary>Initializes the builder with the default ActiveMQ Artemis image and readiness preset.</summary>
    public ArtemisBuilder()
        : base(
            // Docker Hub publishes the Alpine image as 2.57.0-alpine, the newest stable tag at the
            // time of writing (latest-alpine points at the same digest).
            "docker.io/apache/artemis:2.57.0-alpine",
            ArtemisContainer.DefaultPort,
            // The broker logs "AMQ221007: Server is now live" once startup finishes. The message
            // ID is the version-tolerant tail: the surrounding text has changed over releases
            // (2.57.0 says "Server is now active"), while AMQ221007 has stayed with the readiness
            // message. The earlier "starting" lines never carry it, so it cannot pass before the
            // broker accepts clients. Confirm the token against real logs on the first
            // integration run.
            "AMQ221007")
    {
    }

    /// <summary>
    /// Creates the broker instance with this user and password and renders them into the
    /// connection string as userinfo. Without this call clients connect anonymously.
    /// </summary>
    public ArtemisBuilder WithCredentials(string username, string password)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        ArgumentNullException.ThrowIfNull(password);
        _username = username;
        _password = password;
        _credentialsInUri = true;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public ArtemisContainer Build() =>
        new(BuildContainer(), _credentialsInUri ? _username : null, _credentialsInUri ? _password : null);

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // docker-run.sh feeds these to `artemis create` on first start and to the generated
            // artemis-users.properties.
            .WithEnvironment("ARTEMIS_USER", _username)
            .WithEnvironment("ARTEMIS_PASSWORD", _password)
            // The image defaults to --require-login; tests connect without a user by default.
            .WithEnvironment("ANONYMOUS_LOGIN", "true")
            // The image WORKDIR: docker-run.sh creates ./etc and ./bin relative to it.
            .WithWorkingDirectory("/var/lib/artemis-instance")
            // The image ENTRYPOINT (/docker-run.sh) is what creates the broker instance and execs
            // ./bin/artemis, so the configured command is its argument: the image CMD "run".
            // Passing the script path here would append a second /docker-run.sh to the entrypoint.
            .WithCommand("run");
}
