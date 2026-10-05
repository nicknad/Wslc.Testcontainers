namespace Wslc.Testcontainers;

/// <summary>Networking mode of the WSLC session.</summary>
public enum ContainerNetworkMode
{
    /// <summary>Bridged networking with a NIC (default). Ports and network waits apply.</summary>
    Bridged = 0,

    /// <summary>
    /// No NIC: the container is fully isolated. Cannot be combined with ports or network waits.
    /// Detection covers only the built-in TCP/HTTP wait strategies; a custom
    /// <see cref="Waiting.IWaitStrategy"/> that needs the network bypasses this validation.
    /// </summary>
    None = 1,
}
