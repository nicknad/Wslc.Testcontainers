namespace Wslc.Testcontainers.Modules.PostgreSql;

/// <summary>
/// Typed Postgres container: a module wrapper that renders an Npgsql connection string from
/// the dynamic host port mapping.
/// </summary>
public sealed class PostgreSqlContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Postgres listens on by default (5432).</summary>
    public const int DefaultPort = 5432;

    private readonly string _username;
    private readonly string _password;
    private readonly string _database;

    internal PostgreSqlContainer(IWslContainer inner, string username, string password, string database)
        : base(inner)
    {
        _username = username;
        _password = password;
        _database = database;
    }

    /// <summary>
    /// Renders an Npgsql connection string for the running container, e.g.
    /// <c>Host=127.0.0.1;Port=49153;Username=...;Password=...;Database=...</c>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"Host={endpoint.Address};Port={endpoint.Port};Username={_username};Password={_password};Database={_database}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
