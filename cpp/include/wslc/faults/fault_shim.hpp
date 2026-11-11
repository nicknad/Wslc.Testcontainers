#pragma once

#include "wslc/endpoint.hpp"
#include "wslc/exceptions.hpp"

#include <chrono>
#include <memory>

namespace wslc::faults
{

/// <summary>Transport fault that <see cref="FaultShim"/> can inject into relayed TCP connections.</summary>
/// <remarks>
/// Faults are applied to the byte stream, never synthesized at the protocol level: a peer sees a
/// real reset, a real half-close, or a real stall, so client reconnect, keepalive and last-will
/// behavior is exercised honestly.
/// </remarks>
enum class FaultMode
{
    /// <summary>Aborts the connection with linger-to-zero on both legs: the peer observes a TCP reset
    /// (RST), not a clean FIN, the same way a crashed broker or host would appear.</summary>
    Reset,

    /// <summary>Send-shuts the selected direction: the destination peer sees a clean FIN while the
    /// opposite direction keeps flowing (a true half-open connection).</summary>
    Close,

    /// <summary>Stops forwarding chunks in the selected direction while both sockets stay open, so
    /// the peer's keepalive or timeout path runs against a silent link.
    /// <see cref="FaultShim::Clear"/> resumes the stream without dropping the held chunk.</summary>
    Stall,
};

/// <summary>Data-flow direction a fault applies to.</summary>
enum class FaultDirection
{
    /// <summary>From the test client through the relay to the target (broker).</summary>
    ClientToBroker,

    /// <summary>From the target (broker) through the relay back to the test client.</summary>
    BrokerToClient,

    /// <summary>Both directions.</summary>
    Both,
};

/// <summary>
/// In-process TCP relay that listens on Windows loopback, forwards every accepted connection to a
/// target endpoint, and injects honest transport faults (reset, half-close, stall, latency) on
/// demand without any helper inside the container.
/// </summary>
/// <remarks>
/// <para>
/// The relay is protocol-blind and opt-in: it never inspects or fabricates broker protocol
/// messages, and the container endpoint remains usable directly. One relay connection maps 1:1 to
/// one target connection, so a broker observes one session per client session.
/// </para>
/// <para>
/// Attach after the container is started. The relay binds an ephemeral IPv4 loopback port, so it
/// never pins a fixed port and parallel test runs cannot collide. Dispose the relay before the
/// container; after a stop/start cycle the mapped host port may change, so attach a new relay.
/// </para>
/// <para>
/// Bounds: at most 255 concurrently relayed connections, a 16 KiB per-direction buffer, a cap on
/// bytes relayed per connection, and a fixed lifetime cap on accepted connections. A client that
/// exceeds a cap is disconnected instead of consuming unbounded memory or threads.
/// </para>
/// <para>
/// Fault state is process-local to this relay and safe to call from multiple threads. Faults apply
/// to live connections only; armed faults apply to connections accepted afterwards. The type is
/// move-only; destroying or disposing it stops the listener and closes every relayed connection.
/// </para>
/// </remarks>
class FaultShim final
{
public:
    FaultShim(const FaultShim&) = delete;
    FaultShim& operator=(const FaultShim&) = delete;
    FaultShim(FaultShim&& other) noexcept;
    FaultShim& operator=(FaultShim&& other) noexcept;
    ~FaultShim();

    /// <summary>
    /// Binds an ephemeral IPv4 loopback port and starts forwarding accepted connections to
    /// <paramref name="target"/>.
    /// </summary>
    /// <param name="target">Loopback endpoint to dial for each accepted client connection; the host
    /// must be a numeric address.</param>
    /// <returns>A started relay that owns the listener and every relayed socket.</returns>
    /// <exception cref="WslException">The target is invalid or the loopback listener could not be
    /// bound.</exception>
    static FaultShim Attach(WslEndpoint target);

    /// <summary>Gets the loopback endpoint a test client connects to instead of the target.</summary>
    /// <remarks>The bind address is always <c>127.0.0.1</c>; reading the endpoint performs no I/O.</remarks>
    /// <exception cref="WslException">The relay has been disposed.</exception>
    WslEndpoint Endpoint() const;

    /// <summary>Gets the number of currently relayed (open) connections.</summary>
    /// <remarks>This is cheap state: it takes a short lock and performs no I/O.</remarks>
    /// <exception cref="WslException">The relay has been disposed.</exception>
    int LiveConnections() const;

    /// <summary>Applies a fault immediately to every live relayed connection.</summary>
    /// <param name="mode">Fault to apply.</param>
    /// <param name="direction">Direction the fault applies to; defaults to both.</param>
    /// <returns>
    /// The number of live connections the fault was applied to. Zero means nothing was connected;
    /// it is not an error.
    /// </returns>
    /// <exception cref="WslException">The relay has been disposed, or <paramref name="mode"/> or
    /// <paramref name="direction"/> is not a defined value.</exception>
    int Inject(FaultMode mode, FaultDirection direction = FaultDirection::Both);

    /// <summary>Applies a fault at accept time to the next <paramref name="connections"/> clients.</summary>
    /// <remarks>
    /// Replaces any previously armed fault. <paramref name="connections"/> of zero disarms it. The
    /// counter decrements once per accepted relayed connection and never fires again after it
    /// reaches zero, so retry/backoff storms can be made deterministic.
    /// </remarks>
    /// <param name="connections">Number of accepted connections to fault; must be non-negative.</param>
    /// <param name="mode">Fault to apply at accept time.</param>
    /// <param name="direction">Direction the fault applies to; defaults to both.</param>
    /// <exception cref="WslException">The relay has been disposed,
    /// <paramref name="connections"/> is negative, or <paramref name="mode"/> or
    /// <paramref name="direction"/> is not a defined value.</exception>
    void Arm(int connections, FaultMode mode, FaultDirection direction = FaultDirection::Both);

    /// <summary>Delays every relayed chunk in the selected direction by a fixed duration.</summary>
    /// <remarks>
    /// The delay applies to chunks forwarded after the call, including chunks on live connections;
    /// bytes are delayed, never dropped or reordered. A zero delay clears the latency.
    /// </remarks>
    /// <param name="delay">Non-negative per-chunk delay.</param>
    /// <param name="direction">Direction the delay applies to; defaults to both.</param>
    /// <exception cref="WslException">The relay has been disposed, <paramref name="delay"/> is
    /// negative, or <paramref name="direction"/> is not a defined value.</exception>
    void Latency(std::chrono::milliseconds delay, FaultDirection direction = FaultDirection::Both);

    /// <summary>Drops latency and armed faults, and resumes every stalled direction.</summary>
    /// <remarks>
    /// Idempotent and safe when nothing is armed or live. Connections already reset or half-closed
    /// stay that way: those TCP transitions cannot be undone.
    /// </remarks>
    /// <exception cref="WslException">The relay has been disposed.</exception>
    void Clear();

    /// <summary>
    /// Stops accepting, closes every relayed connection, and releases the listener. Idempotent,
    /// best-effort, and never throws (per ADR-0004 best-effort cleanup).
    /// </summary>
    void Dispose() noexcept;

private:
    struct Impl;

    explicit FaultShim(std::unique_ptr<Impl> impl) noexcept;
    void ThrowIfDisposed() const;

    std::unique_ptr<Impl> m_impl;
};

} // namespace wslc::faults
