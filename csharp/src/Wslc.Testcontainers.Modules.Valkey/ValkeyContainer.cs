namespace Wslc.Testcontainers.Modules.Valkey;

/// <summary>
/// Typed Valkey container: a module wrapper that renders a StackExchange.Redis-style endpoint
/// from the dynamic host port mapping.
/// </summary>
public sealed class ValkeyContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Valkey listens on by default (6379).</summary>
    public const int DefaultPort = 6379;

    internal ValkeyContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the <c>host:port</c> endpoint for StackExchange.Redis (e.g. <c>127.0.0.1:49153</c>).</summary>
    public string GetEndpoint() => FormatEndpoint(GetConnectEndpoint(DefaultPort));
}
