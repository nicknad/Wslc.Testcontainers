namespace Wslc.Testcontainers.Modules.MariaDb;

/// <summary>
/// Typed MariaDB container: a module wrapper that renders a MySqlConnector-style connection
/// string from the dynamic host port mapping.
/// </summary>
public sealed class MariaDbContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port MariaDB listens on by default (3306).</summary>
    public const int DefaultPort = 3306;

    private readonly string _username;
    private readonly string _password;
    private readonly string _database;

    internal MariaDbContainer(IWslContainer inner, string username, string password, string database)
        : base(inner)
    {
        _username = username;
        _password = password;
        _database = database;
    }

    /// <summary>
    /// Renders a MySqlConnector connection string for the running container, e.g.
    /// <c>Server=127.0.0.1;Port=49153;User ID=...;Password=...;Database=...</c>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"Server={endpoint.Address};Port={endpoint.Port};User ID={_username};Password={_password};Database={_database}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
