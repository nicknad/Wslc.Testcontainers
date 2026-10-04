namespace Wslc.Testcontainers;

/// <summary>Networking mode of the WSLC session.</summary>
public enum ContainerNetworkMode
{
    /// <summary>Bridged networking with a NIC (default). Ports and network waits apply.</summary>
    Bridged = 0,

    /// <summary>No NIC: the container is fully isolated. Cannot be combined with ports or network waits.</summary>
    None = 1,
}
