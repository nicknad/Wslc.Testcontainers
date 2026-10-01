namespace Wslc.Testcontainers.Waiting;

/// <summary>Waits until an HTTP request against a mapped port succeeds.</summary>
internal sealed record HttpWaitStrategy(string Path, int Port) : PollingWaitStrategyBase
{
    private static readonly HttpClient Client = new() { Timeout = System.Threading.Timeout.InfiniteTimeSpan };

    private readonly string _normalizedPath = Path.StartsWith('/') ? Path : "/" + Path;

    public override string Name => $"HTTP request to '{Path}' on port {Port} to succeed";

    protected override async Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken)
    {
        var mappedPort = target.GetMappedPort(Port);
        var uri = new Uri($"http://{target.Host}:{mappedPort}{_normalizedPath}");

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
