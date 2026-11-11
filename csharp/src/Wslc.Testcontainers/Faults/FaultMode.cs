namespace Wslc.Testcontainers.Faults;

/// <summary>Transport fault that <see cref="FaultShim"/> can inject into relayed TCP connections.</summary>
/// <remarks>
/// Faults are applied to the byte stream, never synthesized at the protocol level: a broker sees
/// a real reset, a real half-close, or a real stall, so client reconnect, keepalive and last-will
/// behavior is exercised honestly.
/// </remarks>
public enum FaultMode
{
    /// <summary>
    /// Aborts the connection with linger-to-zero on both legs: the peer observes a TCP reset
    /// (RST), not a clean FIN, the same way a crashed broker or host would appear.
    /// </summary>
    Reset,

    /// <summary>
    /// Send-shuts the selected direction: the destination peer sees a clean FIN while the
    /// opposite direction keeps flowing (a true half-open connection).
    /// </summary>
    Close,

    /// <summary>
    /// Stops forwarding chunks in the selected direction while both sockets stay open, so the
    /// peer's keepalive/timeout path runs against a silent link. <see cref="FaultShim.Clear"/>
    /// resumes the stream without dropping the held bytes.
    /// </summary>
    Stall,
}
