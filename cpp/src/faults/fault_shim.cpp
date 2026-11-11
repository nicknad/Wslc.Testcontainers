#include "wslc/faults/fault_shim.hpp"

#include "internal/tcp_http.hpp"
#include "internal/util.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace wslc::faults
{

namespace detail
{

namespace
{

// 255 concurrent relayed connections bound the number of accepted sockets and pumps; a test suite
// needs a handful, while a runaway client cannot exhaust the machine's handles.
constexpr int c_maxLiveConnections = 255;

// Per ADR bounded loops the accept loop is capped at this many connections per relay lifetime (well
// beyond any test) and also stops when dispose closes the listener.
constexpr int c_maxAcceptedConnections = 100'000;

// Per ADR bounded loops, each pump relays at most this many chunks: 1 GiB / 16 KiB + 1 bounds both
// the bytes and the iterations so a misbehaving peer cannot pump forever.
constexpr std::size_t c_bufferSize = 16u * 1024u;
constexpr std::size_t c_maxRelayedBytes = 1024ull * 1024ull * 1024ull;
constexpr std::size_t c_maxChunks = c_maxRelayedBytes / c_bufferSize + 1u;

// The accept loop polls with a short select timeout so dispose is observed without relying on a
// closed listening socket aborting a pending accept.
constexpr std::chrono::milliseconds c_acceptSlice{50};

// A dial to a loopback target either completes or is refused immediately; the deadline only keeps a
// black-holed address from parking a relay thread forever.
constexpr std::chrono::milliseconds c_connectTimeout{2000};
constexpr std::chrono::milliseconds c_connectSlice{50};

// Teardown waits for the listener and pumps to observe the closed sockets; 5s is far more than
// loopback needs, and the cap keeps Dispose from hanging a failing test run (ADR-0004).
constexpr std::chrono::milliseconds c_drainTimeout{5000};

// Latency is slept in slices so teardown never waits out a configured delay.
constexpr std::chrono::milliseconds c_delaySlice{10};

// A stalled direction polls its gate in slices. Clear() and teardown are observed within one
// slice, which is far below any test's tolerance, and no lock is held across a blocking call.
constexpr std::chrono::milliseconds c_stallSlice{5};

std::string SocketErrorText()
{
    return internal::FormatWindowsError(static_cast<unsigned long>(WSAGetLastError()));
}

/// <summary>Closes the handle and resets it to the invalid value; safe to repeat.</summary>
void CloseSocket(SOCKET& handle) noexcept
{
    if (!std::cmp_equal(handle, INVALID_SOCKET))
    {
        closesocket(handle);
        handle = INVALID_SOCKET;
    }
}

void SetNoDelay(SOCKET handle) noexcept
{
    int enabled = 1;
    static_cast<void>(
        setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled)));
}

void TryShutdownSend(SOCKET handle) noexcept
{
    if (!std::cmp_equal(handle, INVALID_SOCKET))
    {
        static_cast<void>(shutdown(handle, SD_SEND));
    }
}

void AbortWithReset(SOCKET handle) noexcept
{
    if (std::cmp_equal(handle, INVALID_SOCKET))
    {
        return;
    }

    // Linger-to-zero turns the close into an abortive close, so the peer observes RST instead of the
    // FIN a graceful close would send.
    linger lingerOption{};
    lingerOption.l_onoff = 1;
    lingerOption.l_linger = 0;
    static_cast<void>(
        setsockopt(handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lingerOption), sizeof(lingerOption)));
    closesocket(handle);
}

/// <summary>Resolves a numeric target host to a socket address; false when it is not resolvable.</summary>
bool ResolveTarget(const WslEndpoint& target, sockaddr_storage& address, int& length)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    addrinfo* addresses = nullptr;
    const std::string portText = std::to_string(target.Port);
    if (getaddrinfo(target.Host.c_str(), portText.c_str(), &hints, &addresses) != 0 || addresses == nullptr)
    {
        return false;
    }

    std::memcpy(&address, addresses->ai_addr, addresses->ai_addrlen);
    length = static_cast<int>(addresses->ai_addrlen);
    freeaddrinfo(addresses);
    return true;
}

/// <summary>Connects to the address with a fixed deadline; the returned socket is blocking, or invalid.</summary>
SOCKET ConnectAddress(const sockaddr* address, int length) noexcept
{
    const SOCKET created = ::socket(address->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (std::cmp_equal(created, INVALID_SOCKET))
    {
        return INVALID_SOCKET;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(created, FIONBIO, &nonBlocking) != 0)
    {
        closesocket(created);
        return INVALID_SOCKET;
    }

    bool connected = connect(created, address, length) == 0;
    if (!connected)
    {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL)
        {
            // Bounded by the connect deadline: each pass advances the clock by one select slice.
            const auto deadline = std::chrono::steady_clock::now() + c_connectTimeout;
            while (!connected && std::chrono::steady_clock::now() < deadline)
            {
                fd_set writeSet;
                FD_ZERO(&writeSet);
                FD_SET(created, &writeSet);
                timeval timeoutValue{};
                timeoutValue.tv_sec = 0;
                timeoutValue.tv_usec =
                    static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(c_connectSlice).count());
                const int selected = select(0, nullptr, &writeSet, nullptr, &timeoutValue);
                if (selected > 0)
                {
                    int socketError = 0;
                    int errorLength = static_cast<int>(sizeof(socketError));
                    if (getsockopt(created, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError),
                                   &errorLength) == 0)
                    {
                        connected = socketError == 0;
                    }

                    break;
                }

                if (selected < 0)
                {
                    break;
                }
            }
        }
    }

    if (!connected)
    {
        closesocket(created);
        return INVALID_SOCKET;
    }

    u_long blocking = 0;
    static_cast<void>(ioctlsocket(created, FIONBIO, &blocking));
    return created;
}

/// <summary>Waits for readability with a fixed timeout; -1 on error, 0 on timeout, 1 when readable.</summary>
int SelectReadable(SOCKET handle, std::chrono::milliseconds timeout) noexcept
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(handle, &readSet);
    timeval timeoutValue{};
    timeoutValue.tv_sec = static_cast<long>(timeout.count() / 1000);
    timeoutValue.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    return select(0, &readSet, nullptr, nullptr, &timeoutValue);
}

} // namespace

void ValidateMode(FaultMode mode)
{
    if (mode != FaultMode::Reset && mode != FaultMode::Close && mode != FaultMode::Stall)
    {
        throw WslException("Unknown fault mode.");
    }
}

void ValidateDirection(FaultDirection direction)
{
    if (direction != FaultDirection::ClientToBroker && direction != FaultDirection::BrokerToClient &&
        direction != FaultDirection::Both)
    {
        throw WslException("Unknown fault direction.");
    }
}

class Connection;

/// <summary>
/// Relay state shared by the listener thread, the relay threads it starts and the public calls. It
/// is reference counted so a detached pump can never touch freed state during best-effort teardown.
/// </summary>
struct State final : public std::enable_shared_from_this<State>
{
    ~State();

    void Initialize(const WslEndpoint& target);
    void Start();
    void Shutdown() noexcept;

    WslEndpoint Endpoint() const noexcept;
    int LiveConnections() const;
    int Inject(FaultMode mode, FaultDirection direction);
    void Arm(int connections, FaultMode mode, FaultDirection direction);
    void Latency(std::chrono::milliseconds delay, FaultDirection direction);
    void Clear();
    std::chrono::milliseconds GetLatency(bool clientToBroker) const;
    std::optional<std::pair<FaultMode, FaultDirection>> TakeArmedFault();

    bool Register(const std::shared_ptr<Connection>& connection);
    void Unregister(Connection* connection);

    SOCKET ConnectTarget() const noexcept;

    void AcceptLoop();
    bool TryStartRelay(SOCKET client);
    void Relay(SOCKET client);
    void RelayFinished();

    void AbortConnections();
    void DrainRelays() noexcept;

    SOCKET m_listener = INVALID_SOCKET;
    sockaddr_storage m_targetAddress{};
    int m_targetAddressLength = 0;
    WslEndpoint m_endpoint;
    std::atomic<bool> m_closed{false};
    std::atomic<bool> m_shutdown{false};

    mutable std::mutex m_gate;
    std::vector<std::shared_ptr<Connection>> m_connections;
    std::optional<FaultMode> m_armedMode;
    FaultDirection m_armedDirection = FaultDirection::Both;
    int m_armedRemaining = 0;
    std::chrono::milliseconds m_latency{0};
    FaultDirection m_latencyDirection = FaultDirection::Both;

    std::mutex m_relayGate;
    std::condition_variable m_relaysIdle;
    std::vector<std::thread> m_relays;
    int m_activeRelays = 0;

    std::jthread m_acceptThread;
};

/// <summary>
/// One relayed client/target socket pair. Owns both directional pumps plus the per-direction stall
/// gate and half-close state; the state only routes fault calls here.
/// </summary>
class Connection final : public std::enable_shared_from_this<Connection>
{
public:
    Connection(State& owner, SOCKET client, SOCKET target) noexcept;
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /// <summary>Applies one fault; returns false when the connection already ended.</summary>
    bool Apply(FaultMode mode, FaultDirection direction);

    /// <summary>Resumes stalled directions; safe on connections that were never stalled.</summary>
    void Resume();

    /// <summary>Best-effort teardown: closes both ends without resetting the peer.</summary>
    void Abort();

    /// <summary>Pumps both directions until they end, then closes both sockets.</summary>
    void Pump();

private:
    bool Reset();
    bool HalfClose(FaultDirection direction);
    bool Stall(FaultDirection direction);
    bool Close(bool reset);
    void PumpDirection(SOCKET source, SOCKET destination, bool clientToBroker);
    bool SendAll(SOCKET destination, const char* data, int length) noexcept;
    void DelayOrAbort(std::chrono::milliseconds delay);
    void WaitWhileStalled(bool clientToBroker);
    bool Stopped(bool clientToBroker) const noexcept;
    bool Stalled(bool clientToBroker) const noexcept;

    State& m_owner;
    SOCKET m_client;
    SOCKET m_target;
    std::mutex m_gate;
    std::atomic<bool> m_closed{false};
    std::atomic<bool> m_clientToTargetStopped{false};
    std::atomic<bool> m_targetToClientStopped{false};
    std::atomic<bool> m_clientToTargetStalled{false};
    std::atomic<bool> m_targetToClientStalled{false};
};

Connection::Connection(State& owner, SOCKET client, SOCKET target) noexcept
    : m_owner(owner), m_client(client), m_target(target)
{
}

Connection::~Connection()
{
    std::lock_guard lock(m_gate);
    CloseSocket(m_client);
    CloseSocket(m_target);
}

bool Connection::Apply(FaultMode mode, FaultDirection direction)
{
    switch (mode)
    {
    case FaultMode::Reset:
        return Reset();
    case FaultMode::Close:
        return HalfClose(direction);
    case FaultMode::Stall:
        return Stall(direction);
    default:
        return false;
    }
}

void Connection::Abort()
{
    static_cast<void>(Close(false));
}

void Connection::Resume()
{
    m_clientToTargetStalled.store(false);
    m_targetToClientStalled.store(false);
}

bool Connection::Reset()
{
    return Close(true);
}

bool Connection::Close(bool reset)
{
    {
        // The sockets are closed under the gate so pump cleanup is serialized with the
        // linger-to-zero decision: if reset already won, it sent the RST and this close is a no-op.
        std::lock_guard lock(m_gate);
        if (m_closed.exchange(true))
        {
            return false;
        }

        if (reset)
        {
            // Both legs are aborted for a clean drop, so the peer observes RST rather than FIN.
            AbortWithReset(m_client);
            AbortWithReset(m_target);
            m_client = INVALID_SOCKET;
            m_target = INVALID_SOCKET;
        }
        else
        {
            CloseSocket(m_client);
            CloseSocket(m_target);
        }
    }

    m_owner.Unregister(this);
    return true;
}

bool Connection::HalfClose(FaultDirection direction)
{
    {
        std::lock_guard lock(m_gate);
        if (m_closed.load())
        {
            return false;
        }
    }

    if (direction == FaultDirection::ClientToBroker || direction == FaultDirection::Both)
    {
        // The pump drops the late chunk instead of writing after Shutdown(Send).
        m_clientToTargetStopped.store(true);
        TryShutdownSend(m_target);
    }

    if (direction == FaultDirection::BrokerToClient || direction == FaultDirection::Both)
    {
        m_targetToClientStopped.store(true);
        TryShutdownSend(m_client);
    }

    return true;
}

bool Connection::Stall(FaultDirection direction)
{
    {
        std::lock_guard lock(m_gate);
        if (m_closed.load())
        {
            return false;
        }
    }

    if (direction == FaultDirection::ClientToBroker || direction == FaultDirection::Both)
    {
        m_clientToTargetStalled.store(true);
    }

    if (direction == FaultDirection::BrokerToClient || direction == FaultDirection::Both)
    {
        m_targetToClientStalled.store(true);
    }

    return true;
}

void Connection::Pump()
{
    const auto self = shared_from_this();
    {
        // The pumps join on scope exit, so the close below always happens after both directions end,
        // mirroring the async relay's WhenAll.
        std::jthread clientToTarget([this, self] { PumpDirection(m_client, m_target, true); });
        std::jthread targetToClient([this, self] { PumpDirection(m_target, m_client, false); });
    }

    bool alreadyClosed = false;
    {
        // Close under the gate so a pump that ended wins no race with Reset's linger-to-zero
        // decision: if reset already won, it sent the RST and this close is a no-op.
        std::lock_guard lock(m_gate);
        alreadyClosed = m_closed.exchange(true);
        if (!alreadyClosed)
        {
            CloseSocket(m_client);
            CloseSocket(m_target);
        }
    }

    m_owner.Unregister(this);
}

void Connection::PumpDirection(SOCKET source, SOCKET destination, bool clientToBroker)
{
    std::array<char, c_bufferSize> buffer{};

    // Per ADR bounded loops the chunk cap bounds the relay: 1 GiB / 16 KiB + 1. A peer that exceeds
    // it is treated as broken and the connection is torn down.
    for (std::size_t chunk = 0; chunk < c_maxChunks; ++chunk)
    {
        // Stall gate before the read: a stalled direction stops pulling bytes so the sender's own
        // socket buffer provides backpressure.
        WaitWhileStalled(clientToBroker);
        if (m_closed.load())
        {
            return;
        }

        const int received = recv(source, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received == 0)
        {
            // Propagate the peer's FIN so the other end sees a half-close at the right time.
            TryShutdownSend(destination);
            return;
        }

        if (received < 0)
        {
            // Reset, abort or dispose: both ends are going away, so ending quietly is expected.
            Abort();
            return;
        }

        if (Stopped(clientToBroker))
        {
            // The half-close fault already sent FIN this way; discard the late chunk instead of
            // writing to a socket whose send side is shut down.
            return;
        }

        // Stall gate after the read: the chunk that raced the stall is held, not forwarded, and
        // Clear resumes it without corrupting the byte stream.
        WaitWhileStalled(clientToBroker);
        if (m_closed.load())
        {
            return;
        }

        const auto delay = m_owner.GetLatency(clientToBroker);
        if (delay > std::chrono::milliseconds::zero())
        {
            DelayOrAbort(delay);
            if (m_closed.load())
            {
                return;
            }
        }

        if (!SendAll(destination, buffer.data(), received))
        {
            Abort();
            return;
        }
    }

    Abort();
}

bool Connection::SendAll(SOCKET destination, const char* data, int length) noexcept
{
    int offset = 0;
    // Bounded by the chunk size: every successful send advances the offset by at least one byte.
    while (offset < length)
    {
        const int written = send(destination, data + offset, length - offset, 0);
        if (written <= 0)
        {
            return false;
        }

        offset += written;
    }

    return true;
}

void Connection::DelayOrAbort(std::chrono::milliseconds delay)
{
    // Bounded by the delay itself; sliced so teardown does not have to wait it out.
    for (auto remaining = delay; remaining > std::chrono::milliseconds::zero() && !m_closed.load();
         remaining -= c_delaySlice)
    {
        std::this_thread::sleep_for(std::min(remaining, c_delaySlice));
    }
}

void Connection::WaitWhileStalled(bool clientToBroker)
{
    // Parks while the direction is stalled. Clear() resumes the stream and teardown ends the wait
    // through m_closed, so a pump is never left waiting on a connection that is going away.
    while (!m_closed.load() && Stalled(clientToBroker))
    {
        std::this_thread::sleep_for(c_stallSlice);
    }
}

bool Connection::Stopped(bool clientToBroker) const noexcept
{
    return clientToBroker ? m_clientToTargetStopped.load() : m_targetToClientStopped.load();
}

bool Connection::Stalled(bool clientToBroker) const noexcept
{
    return clientToBroker ? m_clientToTargetStalled.load() : m_targetToClientStalled.load();
}

State::~State()
{
    Shutdown();
}

void State::Initialize(const WslEndpoint& target)
{
    internal::EnsureWinsock();

    if (!ResolveTarget(target, m_targetAddress, m_targetAddressLength))
    {
        throw WslException("Fault shim target '" + target.Host + ":" + std::to_string(target.Port) +
                           "' could not be resolved to a numeric address.");
    }

    // Bind an ephemeral IPv4 loopback port: never pin a fixed port, so parallel test runs cannot
    // collide (ADR-0009).
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (std::cmp_equal(listener, INVALID_SOCKET))
    {
        throw WslException("Fault shim listener could not be created: " + SocketErrorText());
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0)
    {
        const std::string error = SocketErrorText();
        closesocket(listener);
        throw WslException("Fault shim listener could not bind loopback: " + error);
    }

    if (listen(listener, SOMAXCONN) != 0)
    {
        const std::string error = SocketErrorText();
        closesocket(listener);
        throw WslException("Fault shim listener could not listen: " + error);
    }

    int length = static_cast<int>(sizeof(address));
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        const std::string error = SocketErrorText();
        closesocket(listener);
        throw WslException("Fault shim listener address could not be read: " + error);
    }

    m_listener = listener;
    m_endpoint.Host = "127.0.0.1";
    m_endpoint.Port = static_cast<int>(ntohs(address.sin_port));
}

void State::Start()
{
    const auto self = shared_from_this();
    m_acceptThread = std::jthread([self] { self->AcceptLoop(); });
}

void State::Shutdown() noexcept
{
    if (m_shutdown.exchange(true))
    {
        return;
    }

    m_closed.store(true);

    {
        std::lock_guard lock(m_gate);
        CloseSocket(m_listener);
    }

    AbortConnections();

    // The accept loop polls with a short select timeout, so it observes the closed listener (and the
    // closed flag) within one slice; joining it first keeps it from starting a new relay afterwards.
    try
    {
        if (m_acceptThread.joinable())
        {
            m_acceptThread.join();
        }
    }
    catch (...)
    {
        // Best-effort: teardown must not throw to the caller (ADR-0004).
    }

    DrainRelays();
}

void State::AbortConnections()
{
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard lock(m_gate);
        connections.swap(m_connections);
        m_armedMode.reset();
        m_armedRemaining = 0;
        m_latency = std::chrono::milliseconds::zero();
    }

    for (const auto& connection : connections)
    {
        connection->Abort();
    }
}

void State::DrainRelays() noexcept
{
    try
    {
        std::unique_lock lock(m_relayGate);
        static_cast<void>(m_relaysIdle.wait_for(lock, c_drainTimeout, [this] { return m_activeRelays == 0; }));
    }
    catch (...)
    {
        // Best-effort: fall through and detach whatever is still running.
    }

    std::vector<std::thread> relays;
    bool drained = false;
    {
        std::lock_guard lock(m_relayGate);
        relays.swap(m_relays);
        drained = m_activeRelays == 0;
    }

    for (auto& relay : relays)
    {
        if (!relay.joinable())
        {
            continue;
        }

        if (drained)
        {
            try
            {
                relay.join();
                continue;
            }
            catch (...)
            {
                // Best-effort: fall back to detaching below.
            }
        }

        // A pump that ignored the closed sockets must not hang teardown (ADR-0004). Each relay
        // thread keeps this state alive through its own shared_ptr, so detaching is safe.
        relay.detach();
    }
}

WslEndpoint State::Endpoint() const noexcept
{
    return m_endpoint;
}

int State::LiveConnections() const
{
    std::lock_guard lock(m_gate);
    return static_cast<int>(m_connections.size());
}

int State::Inject(FaultMode mode, FaultDirection direction)
{
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard lock(m_gate);
        connections = m_connections;
    }

    int affected = 0;
    for (const auto& connection : connections)
    {
        if (connection->Apply(mode, direction))
        {
            ++affected;
        }
    }

    return affected;
}

void State::Arm(int connections, FaultMode mode, FaultDirection direction)
{
    std::lock_guard lock(m_gate);
    if (connections == 0)
    {
        m_armedMode.reset();
        m_armedRemaining = 0;
        return;
    }

    m_armedMode = mode;
    m_armedDirection = direction;
    m_armedRemaining = connections;
}

void State::Latency(std::chrono::milliseconds delay, FaultDirection direction)
{
    std::lock_guard lock(m_gate);
    m_latency = delay;
    m_latencyDirection = direction;
}

void State::Clear()
{
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard lock(m_gate);
        m_latency = std::chrono::milliseconds::zero();
        m_latencyDirection = FaultDirection::Both;
        m_armedMode.reset();
        m_armedRemaining = 0;
        m_armedDirection = FaultDirection::Both;
        connections = m_connections;
    }

    for (const auto& connection : connections)
    {
        connection->Resume();
    }
}

std::chrono::milliseconds State::GetLatency(bool clientToBroker) const
{
    std::lock_guard lock(m_gate);
    const auto direction = clientToBroker ? FaultDirection::ClientToBroker : FaultDirection::BrokerToClient;
    const bool applies = m_latencyDirection == FaultDirection::Both || m_latencyDirection == direction;
    return applies ? m_latency : std::chrono::milliseconds::zero();
}

std::optional<std::pair<FaultMode, FaultDirection>> State::TakeArmedFault()
{
    std::lock_guard lock(m_gate);
    if (!m_armedMode.has_value() || m_armedRemaining <= 0)
    {
        return std::nullopt;
    }

    --m_armedRemaining;
    const FaultMode mode = *m_armedMode;
    if (m_armedRemaining == 0)
    {
        m_armedMode.reset();
    }

    return std::make_pair(mode, m_armedDirection);
}

bool State::Register(const std::shared_ptr<Connection>& connection)
{
    std::lock_guard lock(m_gate);
    if (m_closed.load())
    {
        return false;
    }

    m_connections.push_back(connection);
    return true;
}

void State::Unregister(Connection* connection)
{
    std::lock_guard lock(m_gate);
    m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                                       [connection](const std::shared_ptr<Connection>& candidate)
                                       { return candidate.get() == connection; }),
                        m_connections.end());
}

SOCKET State::ConnectTarget() const noexcept
{
    return ConnectAddress(reinterpret_cast<const sockaddr*>(&m_targetAddress), m_targetAddressLength);
}

void State::AcceptLoop()
{
    // Per ADR bounded loops the accept loop stops after a fixed number of connections and also stops
    // as soon as dispose closes the listener.
    for (int accepted = 0; accepted < c_maxAcceptedConnections && !m_closed.load(); ++accepted)
    {
        const int selected = SelectReadable(m_listener, c_acceptSlice);
        if (selected == 0)
        {
            continue;
        }

        if (selected < 0)
        {
            // The listener was closed (dispose) or failed: nothing more can be accepted.
            return;
        }

        const SOCKET client = accept(m_listener, nullptr, nullptr);
        if (std::cmp_equal(client, INVALID_SOCKET))
        {
            if (m_closed.load())
            {
                return;
            }

            continue;
        }

        if (!TryStartRelay(client))
        {
            // At the concurrent cap: refuse the client instead of queuing unbounded work.
            closesocket(client);
        }
    }
}

bool State::TryStartRelay(SOCKET client)
{
    const auto self = shared_from_this();
    std::lock_guard lock(m_relayGate);
    if (m_activeRelays >= c_maxLiveConnections)
    {
        return false;
    }

    ++m_activeRelays;
    try
    {
        m_relays.emplace_back([self, client] { self->Relay(client); });
    }
    catch (...)
    {
        --m_activeRelays;
        m_relaysIdle.notify_all();
        return false;
    }

    return true;
}

void State::Relay(SOCKET client)
{
    SOCKET target = INVALID_SOCKET;
    std::shared_ptr<Connection> connection;
    try
    {
        target = ConnectTarget();
        if (std::cmp_equal(target, INVALID_SOCKET))
        {
            // Best-effort: a dial failure closes the client without relaying anything.
            closesocket(client);
            RelayFinished();
            return;
        }

        SetNoDelay(client);
        SetNoDelay(target);

        connection = std::make_shared<Connection>(*this, client, target);
        if (!Register(connection))
        {
            // Dispose won the race: never relay a connection on a disposed relay.
            connection->Abort();
            RelayFinished();
            return;
        }

        if (const auto armed = TakeArmedFault())
        {
            static_cast<void>(connection->Apply(armed->first, armed->second));
        }

        connection->Pump();
    }
    catch (...)
    {
        // Best-effort: a dial failure or dispose race closes both ends without surfacing.
        if (connection != nullptr)
        {
            connection->Abort();
        }
        else
        {
            CloseSocket(client);
            CloseSocket(target);
        }
    }

    RelayFinished();
}

void State::RelayFinished()
{
    std::lock_guard lock(m_relayGate);
    --m_activeRelays;
    m_relaysIdle.notify_all();
}

} // namespace detail

struct FaultShim::Impl
{
    ~Impl() { Dispose(); }

    void Dispose() noexcept
    {
        if (m_disposed.exchange(true))
        {
            return;
        }

        if (m_state)
        {
            m_state->Shutdown();
        }
    }

    std::shared_ptr<detail::State> m_state;
    std::atomic<bool> m_disposed{false};
};

FaultShim::FaultShim(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

FaultShim::FaultShim(FaultShim&& other) noexcept = default;

FaultShim& FaultShim::operator=(FaultShim&& other) noexcept
{
    if (this != &other)
    {
        Dispose();
        m_impl = std::move(other.m_impl);
    }

    return *this;
}

FaultShim::~FaultShim()
{
    Dispose();
}

FaultShim FaultShim::Attach(WslEndpoint target)
{
    if (internal::IsBlank(target.Host))
    {
        throw WslException("Fault shim target host must not be empty.");
    }

    if (target.Port < 1 || target.Port > 65535)
    {
        throw WslException("Fault shim target port must be between 1 and 65535.");
    }

    auto state = std::make_shared<detail::State>();
    state->Initialize(target);
    state->Start();

    auto impl = std::make_unique<Impl>();
    impl->m_state = state;
    return FaultShim(std::move(impl));
}

WslEndpoint FaultShim::Endpoint() const
{
    ThrowIfDisposed();
    return m_impl->m_state->Endpoint();
}

int FaultShim::LiveConnections() const
{
    ThrowIfDisposed();
    return m_impl->m_state->LiveConnections();
}

int FaultShim::Inject(FaultMode mode, FaultDirection direction)
{
    ThrowIfDisposed();
    detail::ValidateMode(mode);
    detail::ValidateDirection(direction);
    return m_impl->m_state->Inject(mode, direction);
}

void FaultShim::Arm(int connections, FaultMode mode, FaultDirection direction)
{
    ThrowIfDisposed();
    if (connections < 0)
    {
        throw WslException("Fault shim arm count must not be negative.");
    }

    detail::ValidateMode(mode);
    detail::ValidateDirection(direction);
    m_impl->m_state->Arm(connections, mode, direction);
}

void FaultShim::Latency(std::chrono::milliseconds delay, FaultDirection direction)
{
    ThrowIfDisposed();
    if (delay < std::chrono::milliseconds::zero())
    {
        throw WslException("Fault shim latency must not be negative.");
    }

    detail::ValidateDirection(direction);
    m_impl->m_state->Latency(delay, direction);
}

void FaultShim::Clear()
{
    ThrowIfDisposed();
    m_impl->m_state->Clear();
}

void FaultShim::Dispose() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Dispose();
    }
}

void FaultShim::ThrowIfDisposed() const
{
    if (m_impl == nullptr || m_impl->m_disposed.load())
    {
        throw WslException("The fault shim has been disposed.");
    }
}

} // namespace wslc::faults
