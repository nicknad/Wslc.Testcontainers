namespace Wslc.Testcontainers.Modules.MongoDb;

/// <summary>
/// Testcontainers-style builder for MongoDB. Encapsulates the image, credentials, port and
/// readiness waits so tests don't memorize them. The default is a single server without
/// authentication; set both <see cref="WithUsername"/> and <see cref="WithPassword"/> to create
/// the root user instead. Defaults are for local tests only.
/// </summary>
public sealed class MongoDbBuilder : WslModuleBuilder<MongoDbBuilder>
{
    private string? _username;
    private string? _password;

    /// <summary>Initializes the builder with the default MongoDB image and readiness preset.</summary>
    public MongoDbBuilder()
        : base(
            "docker.io/library/mongo:8",
            MongoDbContainer.DefaultPort,
            "Waiting for connections")
    {
    }

    /// <summary>Creates the root user with this name (in the <c>admin</c> database). Requires <see cref="WithPassword"/>.</summary>
    public MongoDbBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    /// <summary>Sets the root password. Requires <see cref="WithUsername"/>.</summary>
    public MongoDbBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public MongoDbContainer Build()
    {
        if ((_username is null) != (_password is null))
        {
            throw new WslException("MongoDB credentials need both WithUsername and WithPassword.");
        }

        return new(BuildContainer(), _username, _password);
    }

    // With credentials the entrypoint starts a temporary server to create the root user, logs
    // readiness, shuts it down, and starts the real server. Accepting the first occurrence would
    // let the test connect to the temp server and get dropped mid-initialization, so require the
    // second, real readiness message. Without credentials the single server logs it once.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => _username is null ? 1 : 2;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        if (_username is not null)
        {
            builder
                .WithEnvironment("MONGO_INITDB_ROOT_USERNAME", _username)
                .WithEnvironment("MONGO_INITDB_ROOT_PASSWORD", _password!);
        }

        // The image entrypoint maps the configured command to its CMD (mongod).
        return builder.WithCommand("mongod");
    }
}
