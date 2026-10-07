namespace Wslc.Testcontainers.Modules.Keycloak;

/// <summary>
/// Typed Keycloak container: a module wrapper that renders the HTTP base URL and the bootstrap
/// admin credentials from the dynamic host port mapping.
/// </summary>
public sealed class KeycloakContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Keycloak serves HTTP on by default (8080).</summary>
    public const int DefaultPort = 8080;

    private readonly string _adminUsername;
    private readonly string _adminPassword;

    internal KeycloakContainer(IWslContainer inner, string adminUsername, string adminPassword)
        : base(inner)
    {
        _adminUsername = adminUsername;
        _adminPassword = adminPassword;
    }

    /// <summary>Renders the Keycloak HTTP base URL (e.g. <c>http://127.0.0.1:49153</c>).</summary>
    public string GetEndpoint()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"http://{endpoint.Address}:{endpoint.Port}";
    }

    /// <summary>The bootstrap admin username clients must authenticate with.</summary>
    public string AdminUsername => _adminUsername;

    /// <summary>The bootstrap admin password clients must authenticate with.</summary>
    public string AdminPassword => _adminPassword;

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
