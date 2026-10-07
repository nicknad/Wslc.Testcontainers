using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.ClickHouse;

/// <summary>
/// Testcontainers-style builder for ClickHouse. Encapsulates the image, credentials, ports and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// <see cref="WithUsername"/>/<see cref="WithPassword"/>/<see cref="WithDatabase"/> for
/// anything shared.
/// </summary>
public sealed class ClickHouseBuilder : WslModuleBuilder<ClickHouseBuilder>
{
    private string _username = "default";
    private string _password = string.Empty;
    private string _database = "default";

    /// <summary>Initializes the builder with the default ClickHouse image and readiness preset.</summary>
    public ClickHouseBuilder()
        : base(
            "docker.io/clickhouse/clickhouse-server:26.7",
            ClickHouseContainer.HttpPort,
            string.Empty)
    {
    }

    /// <summary>Sets the <c>CLICKHOUSE_USER</c> value. Defaults to <c>default</c>.</summary>
    public ClickHouseBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    /// <summary>Sets the <c>CLICKHOUSE_PASSWORD</c> value. Defaults to an empty password; override for anything shared.</summary>
    public ClickHouseBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    /// <summary>Sets the <c>CLICKHOUSE_DB</c> value. Defaults to <c>default</c>.</summary>
    public ClickHouseBuilder WithDatabase(string database)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(database);
        _database = database;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public ClickHouseContainer Build() => new(BuildContainer(), _username, _password, _database);

    // ClickHouse logs to files rather than a stable stdout readiness line, so readiness is the
    // /ping HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        builder
            .WithEnvironment("CLICKHOUSE_USER", _username)
            .WithEnvironment("CLICKHOUSE_DB", _database)
            // The entrypoint only writes users.d and removes the default user's localhost-only
            // restriction when one of user/password/access-management is set, so opt into access
            // management for the default (empty password) case as well.
            .WithEnvironment("CLICKHOUSE_DEFAULT_ACCESS_MANAGEMENT", "1")
            .WithPort(ClickHouseContainer.NativePort);
        if (_password.Length > 0)
        {
            // The entrypoint treats an unset and an empty password the same; leaving the variable
            // out keeps empty-password setups identical to the image default.
            builder.WithEnvironment("CLICKHOUSE_PASSWORD", _password);
        }

        // The image entrypoint only takes its server path (chown, user setup, database init)
        // when it gets no arguments or an argument that starts with "--"; the configured command
        // becomes its first argument. "clickhouse-server" would be exec'd directly and fall back
        // to ClickHouse's embedded config (loopback-only, no users.d), so pass an ignored root flag.
        return builder
            .WithCommand("--")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/ping", ClickHouseContainer.HttpPort));
    }
}
