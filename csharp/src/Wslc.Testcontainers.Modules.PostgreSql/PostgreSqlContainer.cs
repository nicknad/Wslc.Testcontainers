namespace Wslc.Testcontainers.Modules.PostgreSql;

/// <summary>
/// Typed Postgres container: a module wrapper that renders an Npgsql connection string from
/// the dynamic host port mapping.
/// </summary>
public sealed class PostgreSqlContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Postgres listens on (5432).</summary>
    public const int ContainerPort = 5432;

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

    /// <summary>Gets the Windows port mapped to <see cref="ContainerPort"/>.</summary>
    public int GetMappedPort() => base.GetMappedPort(ContainerPort);

    /// <summary>Renders an Npgsql connection string for the running container.</summary>
    public string GetConnectionString() =>
        $"Host={Host};Port={GetMappedPort()};Username={_username};Password={_password};Database={_database}";
}
