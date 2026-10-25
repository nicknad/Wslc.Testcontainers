namespace Wslc.Testcontainers.Modules.SqlServer;

/// <summary>
/// Typed SQL Server container: a module wrapper that renders a Microsoft.Data.SqlClient
/// connection string from the dynamic host port mapping.
/// </summary>
public sealed class SqlServerContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port SQL Server listens on by default (1433).</summary>
    public const int DefaultPort = 1433;

    private readonly string _password;

    internal SqlServerContainer(IWslContainer inner, string password)
        : base(inner)
    {
        _password = password;
    }

    /// <summary>
    /// Renders a Microsoft.Data.SqlClient connection string for the running container, e.g.
    /// <c>Server=127.0.0.1,49153;Database=master;User Id=sa;Password=...;TrustServerCertificate=True</c>.
    /// The connection targets <c>master</c>; create additional databases with <see cref="WslModuleContainer.ExecAsync"/>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"Server={FormatHost(endpoint.Address)},{endpoint.Port};Database=master;User Id=sa;Password={_password};TrustServerCertificate=True";
    }
}
