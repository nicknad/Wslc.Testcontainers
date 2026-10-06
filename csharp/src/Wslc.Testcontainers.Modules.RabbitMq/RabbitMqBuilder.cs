namespace Wslc.Testcontainers.Modules.RabbitMq;

/// <summary>
/// Testcontainers-style builder for RabbitMQ. Encapsulates the image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// <see cref="WithUsername"/>/<see cref="WithPassword"/> for anything shared.
/// </summary>
public sealed class RabbitMqBuilder : WslModuleBuilder<RabbitMqBuilder>
{
    private string _username = "rabbit";
    private string _password = "secret";

    /// <summary>Initializes the builder with the default RabbitMQ image and readiness preset.</summary>
    public RabbitMqBuilder()
        : base(
            "docker.io/library/rabbitmq:4-alpine",
            RabbitMqContainer.DefaultPort,
            "Server startup complete")
    {
    }

    /// <summary>Sets the <c>RABBITMQ_DEFAULT_USER</c> value. Defaults to <c>rabbit</c>.</summary>
    public RabbitMqBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    /// <summary>Sets the <c>RABBITMQ_DEFAULT_PASS</c> value. Defaults to <c>secret</c>; override for anything shared.</summary>
    public RabbitMqBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public RabbitMqContainer Build() => new(BuildContainer(), _username, _password);

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            .WithEnvironment("RABBITMQ_DEFAULT_USER", _username)
            .WithEnvironment("RABBITMQ_DEFAULT_PASS", _password)
            // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh rabbitmq-server).
            // The script re-execs itself as the rabbitmq user via su-exec before starting
            // the broker.
            .WithCommand("/usr/local/bin/docker-entrypoint.sh", "rabbitmq-server");
}
