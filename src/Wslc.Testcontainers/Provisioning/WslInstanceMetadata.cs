namespace Wslc.Testcontainers.Provisioning;

/// <summary>Ownership metadata persisted next to every WSLC-created session.</summary>
internal sealed record WslInstanceMetadata(
    string SessionId,
    string InstanceId,
    int OwnerProcessId,
    DateTimeOffset CreatedAt)
{
    public string State { get; init; } = "Created";

    public string? Owner { get; init; }

    public string? Image { get; init; }

    public bool Reuse { get; init; }
}
