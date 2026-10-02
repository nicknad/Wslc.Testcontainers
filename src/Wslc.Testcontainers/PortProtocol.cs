namespace Wslc.Testcontainers;

/// <summary>Transport protocol of a mapped Linux service port.</summary>
public enum PortProtocol
{
    /// <summary>Transmission Control Protocol (default).</summary>
    Tcp = 0,

    /// <summary>
    /// User Datagram Protocol. Currently unsupported: the WSLC runtime returns <c>E_NOTIMPL</c>
    /// for UDP mappings, so declarations are rejected. Kept for inspect-payload resolution and
    /// forward compatibility.
    /// </summary>
    Udp = 1,
}
