namespace Wslc.Testcontainers;

/// <summary>Transport protocol of a mapped Linux service port.</summary>
public enum PortProtocol
{
    /// <summary>Transmission Control Protocol (default).</summary>
    Tcp = 0,

    /// <summary>User Datagram Protocol.</summary>
    Udp = 1,
}
