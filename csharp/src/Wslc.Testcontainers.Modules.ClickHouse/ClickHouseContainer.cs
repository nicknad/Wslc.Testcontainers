namespace Wslc.Testcontainers.Modules.ClickHouse;

/// <summary>
/// Typed ClickHouse container: a module wrapper that renders a ClickHouse.Client-style
/// connection string from the dynamic host port mapping.
/// </summary>
public sealed class ClickHouseContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port the ClickHouse HTTP interface listens on by default (8123).</summary>
    public const int HttpPort = 8123;

    /// <summary>The Linux TCP port the ClickHouse native protocol listens on by default (9000).</summary>
    public const int NativePort = 9000;

    private readonly string _username;
    private readonly string _password;
    private readonly string _database;

    internal ClickHouseContainer(IWslContainer inner, string username, string password, string database)
        : base(inner)
    {
        _username = username;
        _password = password;
        _database = database;
    }

    /// <summary>
    /// Renders a ClickHouse.Client connection string for the running container's HTTP port, e.g.
    /// <c>Host=127.0.0.1;Port=49153;Username=default;Password=;Database=default</c>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(HttpPort);
        return $"Host={endpoint.Address};Port={endpoint.Port};Username={_username};Password={_password};Database={_database}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
