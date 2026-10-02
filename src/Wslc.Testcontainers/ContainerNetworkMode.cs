namespace Wslc.Testcontainers;

/// <summary>Networking mode of the WSLC session.</summary>
public enum ContainerNetworkMode
{
    /// <summary>Bridged networking with a NIC (default). Ports, network waits and egress allowlists apply.</summary>
    Bridged = 0,

    /// <summary>No NIC: the container is fully isolated. Cannot be combined with ports, network waits or egress allowlists.</summary>
    None = 1,
}
