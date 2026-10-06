namespace Wslc.Testcontainers.Modules.RustFs;

/// <summary>
/// Typed RustFS container: a module wrapper that renders the S3 endpoint and credentials for an
/// AWS-style client configuration.
/// </summary>
public sealed class RustFsContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port the RustFS S3 API listens on by default (9000).</summary>
    public const int S3Port = 9000;

    /// <summary>The Linux TCP port the RustFS web console listens on by default (9001).</summary>
    public const int ConsolePort = 9001;

    private readonly string _accessKey;
    private readonly string _secretKey;

    internal RustFsContainer(IWslContainer inner, string accessKey, string secretKey)
        : base(inner)
    {
        _accessKey = accessKey;
        _secretKey = secretKey;
    }

    /// <summary>Renders the S3 service URL (e.g. <c>http://127.0.0.1:49153</c>) for a client's service/endpoint configuration.</summary>
    public string GetEndpoint()
    {
        var endpoint = GetConnectEndpoint(S3Port);
        return $"http://{endpoint.Address}:{endpoint.Port}";
    }

    /// <summary>The access key clients must authenticate with.</summary>
    public string AccessKey => _accessKey;

    /// <summary>The secret key clients must authenticate with.</summary>
    public string SecretKey => _secretKey;

    /// <inheritdoc />
    public override ValueTask DisposeAsync() => DisposeInnerAsync();
}
