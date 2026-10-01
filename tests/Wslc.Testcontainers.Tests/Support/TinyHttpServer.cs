using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Wslc.Testcontainers.Tests.Support;

/// <summary>Minimal loopback HTTP server used to exercise the HTTP wait strategy.</summary>
internal sealed class TinyHttpServer : IDisposable
{
    private readonly TcpListener _listener;
    private readonly int _statusCode;
    private readonly CancellationTokenSource _lifetime = new();

    public TinyHttpServer(int statusCode = 200)
    {
        _statusCode = statusCode;
        _listener = new TcpListener(IPAddress.Loopback, 0);
        _listener.Start();
        Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
        _ = AcceptLoopAsync();
    }

    public int Port { get; }

    public void Dispose()
    {
        _lifetime.Cancel();
        try
        {
            _listener.Stop();
        }
        catch
        {
        }

        _lifetime.Dispose();
    }

    private async Task AcceptLoopAsync()
    {
        while (!_lifetime.IsCancellationRequested)
        {
            TcpClient client;
            try
            {
                client = await _listener.AcceptTcpClientAsync(_lifetime.Token).ConfigureAwait(false);
            }
            catch
            {
                return;
            }

            _ = HandleAsync(client);
        }
    }

    private async Task HandleAsync(TcpClient client)
    {
        using (client)
        {
            try
            {
                var stream = client.GetStream();
                var buffer = new byte[4096];
                await stream.ReadAsync(buffer, _lifetime.Token).ConfigureAwait(false);
                const string body = "ok";
                var response =
                    $"HTTP/1.1 {_statusCode} Status\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n{body}";
                await stream.WriteAsync(Encoding.ASCII.GetBytes(response), _lifetime.Token).ConfigureAwait(false);
            }
            catch
            {
            }
        }
    }
}
