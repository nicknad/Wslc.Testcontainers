namespace Wslc.Testcontainers.Modules.MariaDb;

/// <summary>
/// Testcontainers-style builder for MariaDB. Encapsulates the image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// <see cref="WithUsername"/>/<see cref="WithPassword"/>/<see cref="WithDatabase"/> for
/// anything shared.
/// </summary>
public sealed class MariaDbBuilder : WslModuleBuilder<MariaDbBuilder>
{
    private string _username = "mariadb";
    private string _password = "secret";
    private string _database = "customers";

    /// <summary>Initializes the builder with the default MariaDB image and readiness preset.</summary>
    public MariaDbBuilder()
        : base(
            "docker.io/library/mariadb:11.4",
            MariaDbContainer.DefaultPort,
            "ready for connections")
    {
    }

    /// <summary>Sets the <c>MARIADB_USER</c> value. Defaults to <c>mariadb</c>; use <c>root</c> for the server root account.</summary>
    public MariaDbBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    /// <summary>Sets the password for the configured user (and the root account). Defaults to <c>secret</c>; override for anything shared.</summary>
    public MariaDbBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    /// <summary>Sets the <c>MARIADB_DATABASE</c> value. Defaults to <c>customers</c>.</summary>
    public MariaDbBuilder WithDatabase(string database)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(database);
        _database = database;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public MariaDbContainer Build() => new(BuildContainer(), _username, _password, _database);

    // Like the Postgres entrypoint, MariaDB starts a temporary server to run init scripts,
    // logs readiness, shuts it down, and starts the real server. Accepting the first
    // occurrence would let the test connect to the temp server and get dropped
    // mid-initialization, so require the second, real readiness message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 2;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        builder
            .WithEnvironment("MARIADB_ROOT_PASSWORD", _password)
            .WithEnvironment("MARIADB_DATABASE", _database)
            // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh mariadbd).
            .WithCommand("/usr/local/bin/docker-entrypoint.sh", "mariadbd");

        // The entrypoint rejects MARIADB_USER=root; that account is created by
        // MARIADB_ROOT_PASSWORD alone.
        if (_username != "root")
        {
            builder
                .WithEnvironment("MARIADB_USER", _username)
                .WithEnvironment("MARIADB_PASSWORD", _password);
        }

        return builder;
    }
}
