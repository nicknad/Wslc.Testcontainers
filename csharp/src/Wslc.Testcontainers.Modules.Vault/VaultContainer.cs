namespace Wslc.Testcontainers.Modules.Vault;

/// <summary>
/// Typed Vault container: a module wrapper that renders the HTTP address and root token for a
/// Vault client configuration.
/// </summary>
public sealed class VaultContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Vault's HTTP API listens on by default (8200).</summary>
    public const int DefaultPort = 8200;

    private readonly string _rootToken;

    internal VaultContainer(IWslContainer inner, string rootToken)
        : base(inner)
    {
        _rootToken = rootToken;
    }

    /// <summary>Renders the Vault server address (e.g. <c>http://127.0.0.1:49153</c>) for <c>VAULT_ADDR</c>.</summary>
    public string GetAddress() => FormatHttpEndpoint(GetConnectEndpoint(DefaultPort));

    /// <summary>The dev-mode root token clients must authenticate with.</summary>
    public string RootToken => _rootToken;
}
