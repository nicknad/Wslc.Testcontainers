namespace Wslc.Testcontainers.Modules.PostgreSql;

// Testcontainers-style builder for Postgres. Encapsulates the image,
// credentials, port, and readiness waits so tests don't memorize them.
// NOTE: defaults (postgres/secret/customers) are for local tests only; override
// WithPassword/WithUsername/WithDatabase for anything shared.
public sealed class PostgreSqlBuilder : WslModuleBuilder<PostgreSqlBuilder>
{
    private string _username = "postgres";
    private string _password = "secret";
    private string _database = "customers";

    public PostgreSqlBuilder()
        : base(
            "docker.io/library/postgres:15-alpine",
            PostgreSqlContainer.ContainerPort,
            "database system is ready to accept connections")
    {
    }

    public PostgreSqlBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    public PostgreSqlBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    public PostgreSqlBuilder WithDatabase(string database)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(database);
        _database = database;
        return this;
    }

    public PostgreSqlContainer Build() => new(BuildContainer(), _username, _password, _database);

    // The Postgres entrypoint starts a temporary server to run init scripts, logs readiness,
    // then shuts it down and starts the real server. Accepting the first occurrence lets the
    // test connect to the temp server and get dropped mid-initialization, so require the
    // second, real readiness message.
    protected override int ReadyMessageOccurrences => 2;

    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            .WithEnvironment("POSTGRES_USER", _username)
            .WithEnvironment("POSTGRES_PASSWORD", _password)
            .WithEnvironment("POSTGRES_DB", _database)
            // Replicates the image ENTRYPOINT/CMD (docker-entrypoint.sh postgres).
            // The container init process replaces the keep-alive shell; without this
            // the server never starts and readiness can never succeed.
            .WithCommand("/usr/local/bin/docker-entrypoint.sh", "postgres");
}
