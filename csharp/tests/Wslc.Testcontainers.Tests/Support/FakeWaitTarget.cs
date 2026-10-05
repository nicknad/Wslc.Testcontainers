using System.Net;
using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Tests.Support;

internal sealed class FakeWaitTarget : IWaitTarget
{
    public string Name => "fake-container";

    public IPEndPoint ConnectEndpoint { get; set; } = new(IPAddress.Loopback, 15000);

    public Func<string, string[], CancellationToken, Task<ExecResult>>? ExecHandler { get; set; }

    public Func<int, CancellationToken, Task<bool>>? PortHandler { get; set; }

    public Func<string, CancellationToken, Task<bool>>? ProcessHandler { get; set; }

    public List<LogLine> Logs { get; } = new();

    public IPEndPoint GetConnectEndpoint(int containerPort) => ConnectEndpoint;

    public Task<ExecResult> ExecAsync(string command, string[] arguments, CancellationToken cancellationToken) =>
        ExecHandler?.Invoke(command, arguments, cancellationToken)
        ?? Task.FromResult(new ExecResult(0, string.Empty, string.Empty));

    public Task<bool> IsTcpPortOpenAsync(int containerPort, CancellationToken cancellationToken) =>
        PortHandler?.Invoke(containerPort, cancellationToken) ?? Task.FromResult(false);

    public Task<bool> IsProcessRunningAsync(string processName, CancellationToken cancellationToken) =>
        ProcessHandler?.Invoke(processName, cancellationToken) ?? Task.FromResult(false);

    public IReadOnlyList<LogLine> GetRecentLogs() => Logs.ToArray();
}
