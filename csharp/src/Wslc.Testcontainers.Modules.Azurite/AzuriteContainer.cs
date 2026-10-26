namespace Wslc.Testcontainers.Modules.Azurite;

/// <summary>
/// Typed Azurite container: a module wrapper that renders the Blob, Queue and Table endpoints and
/// the development connection string from the dynamic host port mappings.
/// </summary>
public sealed class AzuriteContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Azurite's Blob service listens on by default (10000).</summary>
    public const int BlobPort = 10000;

    /// <summary>The Linux TCP port Azurite's Queue service listens on by default (10001).</summary>
    public const int QueuePort = 10001;

    /// <summary>The Linux TCP port Azurite's Table service listens on by default (10002).</summary>
    public const int TablePort = 10002;

    /// <summary>The well-known Azurite development account name.</summary>
    public const string AccountName = "devstoreaccount1";

    /// <summary>The public Azurite development account key. Not a secret; for test use only.</summary>
    public const string AccountKey =
        "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";

    internal AzuriteContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>
    /// Renders the Blob service endpoint including the account path, e.g.
    /// <c>http://127.0.0.1:49153/devstoreaccount1</c>.
    /// </summary>
    public string GetBlobEndpoint() => FormatServiceEndpoint(BlobPort);

    /// <summary>
    /// Renders the Queue service endpoint including the account path, e.g.
    /// <c>http://127.0.0.1:49154/devstoreaccount1</c>.
    /// </summary>
    public string GetQueueEndpoint() => FormatServiceEndpoint(QueuePort);

    /// <summary>
    /// Renders the Table service endpoint including the account path, e.g.
    /// <c>http://127.0.0.1:49155/devstoreaccount1</c>.
    /// </summary>
    public string GetTableEndpoint() => FormatServiceEndpoint(TablePort);

    /// <summary>
    /// Renders the development storage connection string for the running container, with explicit
    /// endpoints because the Windows host ports are dynamic.
    /// </summary>
    public string GetConnectionString() =>
        $"DefaultEndpointsProtocol=http;AccountName={AccountName};AccountKey={AccountKey};"
        + $"BlobEndpoint={GetBlobEndpoint()};QueueEndpoint={GetQueueEndpoint()};TableEndpoint={GetTableEndpoint()};";

    private string FormatServiceEndpoint(int port) => $"{FormatHttpEndpoint(GetConnectEndpoint(port))}/{AccountName}";
}
