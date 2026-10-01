namespace Wslc.Testcontainers.Waiting;

/// <summary>Waits until a Linux TCP port accepts connections.</summary>
internal sealed record TcpPortWaitStrategy(int Port) : PollingWaitStrategyBase
{
    public override string Name => $"TCP port {Port} to be available";

    protected override Task<bool> CheckAsync(IWaitTarget target, CancellationToken cancellationToken) =>
        target.IsTcpPortOpenAsync(Port, cancellationToken);
}
