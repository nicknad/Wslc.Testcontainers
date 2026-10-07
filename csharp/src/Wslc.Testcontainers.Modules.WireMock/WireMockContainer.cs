namespace Wslc.Testcontainers.Modules.WireMock;

/// <summary>
/// Typed WireMock container: a module wrapper that renders the HTTP stub/admin base URL from the
/// dynamic host port mapping.
/// </summary>
public sealed class WireMockContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port WireMock serves stubs and the admin API on by default (8080).</summary>
    public const int DefaultPort = 8080;

    internal WireMockContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the WireMock HTTP base URL (e.g. <c>http://127.0.0.1:49153</c>).</summary>
    public string GetEndpoint()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        return $"http://{endpoint.Address}:{endpoint.Port}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
