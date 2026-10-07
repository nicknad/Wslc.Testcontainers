namespace Wslc.Testcontainers.Modules.Qdrant;

/// <summary>
/// Typed Qdrant container: a module wrapper that renders the HTTP API endpoint from the dynamic
/// host port mapping.
/// </summary>
public sealed class QdrantContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Qdrant serves its HTTP API on by default (6333).</summary>
    public const int HttpPort = 6333;

    /// <summary>The Linux TCP port Qdrant serves its gRPC API on by default (6334).</summary>
    public const int GrpcPort = 6334;

    internal QdrantContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the Qdrant HTTP API base URL (e.g. <c>http://127.0.0.1:49153</c>).</summary>
    public string GetEndpoint()
    {
        var endpoint = GetConnectEndpoint(HttpPort);
        return $"http://{endpoint.Address}:{endpoint.Port}";
    }

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
