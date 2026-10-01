namespace Wslc.Testcontainers.Modules.PostgreSql;

// Typed wrapper around the core container. Composes IWslContainer and renders
// an Npgsql connection string from the dynamic host port mapping.
public sealed class PostgreSqlContainer : WslModuleContainer
{
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

    public int GetMappedPort() => base.GetMappedPort(ContainerPort);

    public string GetConnectionString() =>
        $"Host={Host};Port={GetMappedPort()};Username={_username};Password={_password};Database={_database}";
}
