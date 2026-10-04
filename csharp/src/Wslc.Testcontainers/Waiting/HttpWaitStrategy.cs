using System.Net;
using System.Net.Sockets;

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
        var mappedPort = target.GetMappedPort(Port);
        if (!Uri.TryCreate($"http://{FormatAuthority(target.GetProbeHost(Port))}:{mappedPort}{_normalizedPath}", UriKind.Absolute, out var uri))
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

    /// <summary>IPv6 literals must be bracketed inside a URI authority.</summary>
    private static string FormatAuthority(string host) =>
        IPAddress.TryParse(host, out var address) && address.AddressFamily == AddressFamily.InterNetworkV6
            ? string.Concat("[", host, "]")
            : host;
}
