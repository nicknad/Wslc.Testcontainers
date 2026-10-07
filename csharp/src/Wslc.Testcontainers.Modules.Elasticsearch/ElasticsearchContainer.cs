namespace Wslc.Testcontainers.Modules.Elasticsearch;

/// <summary>
/// Typed Elasticsearch container: a module wrapper that renders the HTTP endpoint for a client or
/// REST call.
/// </summary>
public sealed class ElasticsearchContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port the Elasticsearch HTTP API listens on by default (9200).</summary>
    public const int DefaultPort = 9200;

    internal ElasticsearchContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the HTTP endpoint (e.g. <c>http://127.0.0.1:49153</c>) for a client or REST call.</summary>
    public string GetEndpoint() => FormatHttpEndpoint(GetConnectEndpoint(DefaultPort));
}
