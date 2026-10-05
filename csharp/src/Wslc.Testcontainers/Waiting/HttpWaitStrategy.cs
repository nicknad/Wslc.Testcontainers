namespace Wslc.Testcontainers.Waiting;

/// <summary>Waits until an HTTP request against a mapped port succeeds.</summary>
internal sealed record HttpWaitStrategy(string Path, int Port) : PollingWaitStrategyBase
{
    // Redirects are not followed: a container can point Location at host-only or link-local
    // addresses, turning the readiness probe into an SSRF primitive. 3xx still counts as success.
    private static readonly HttpClient Client =
        new(new SocketsHttpHandler { AllowAutoRedirect = false }) { Timeout = System.Threading.Timeout.InfiniteTimeSpan };

    private readonly string _normalizedPath = Path.StartsWith('/') ? Path : "/" + Path;

    public override string Name => $"HTTP request to '{Path}' on port {Port} to succeed";

    protected override async Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        // IPEndPoint.ToString() brackets IPv6 literals for the URI authority.
        var endpoint = target.GetConnectEndpoint(Port);
        if (!Uri.TryCreate($"http://{endpoint}{_normalizedPath}", UriKind.Absolute, out var uri))
        {
            // A path that cannot form a valid URI can never be satisfied.
            return false;
        }

        try
        {
            using var response = await Client
                .GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancellationToken)
                .ConfigureAwait(false);
            return (int)response.StatusCode < 500;
        }
        catch (HttpRequestException)
        {
            return false;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return false;
        }
    }
}
