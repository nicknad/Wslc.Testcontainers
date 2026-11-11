#include <gtest/gtest.h>

#include "internal/tcp_http.hpp"
#include "wslc/faults/fault_shim.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using namespace std::chrono_literals;
using wslc::WslEndpoint;
using wslc::WslException;
using wslc::faults::FaultDirection;
using wslc::faults::FaultMode;
using wslc::faults::FaultShim;

namespace
{

constexpr DWORD c_socketTimeoutMs = 5000;
constexpr int c_echoBufferSize = 4096;
constexpr int c_maxEchoChunks = 1024;
constexpr int c_maxEchoConnections = 32;
constexpr int c_maxWaitAttempts = 500;
constexpr std::chrono::milliseconds c_waitSlice{10};
constexpr std::chrono::milliseconds c_acceptSlice{10};
constexpr std::string_view c_byeMarker = "bye";

void SetNoDelay(SOCKET handle) noexcept
{
    int enabled = 1;
    static_cast<void>(
        setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled)));
}

bool SendAll(SOCKET handle, const char* data, int length) noexcept
{
    int offset = 0;
    // Bounded by the payload: every successful send advances the offset by at least one byte.
    while (offset < length)
    {
        const int written = send(handle, data + offset, length - offset, 0);
        if (written <= 0)
        {
            return false;
        }

        offset += written;
    }

    return true;
}

void SendText(SOCKET handle, const std::string& text)
{
    if (!SendAll(handle, text.data(), static_cast<int>(text.size())))
    {
        throw std::runtime_error("send failed: socket error " + std::to_string(WSAGetLastError()));
    }
}

std::string ReceiveExactly(SOCKET handle, int count)
{
    std::string data(static_cast<std::size_t>(count), '\0');
    int received = 0;
    while (received < count)
    {
        const int read = recv(handle, data.data() + received, count - received, 0);
        if (read <= 0)
        {
            throw std::runtime_error("recv failed after " + std::to_string(received) + " of " + std::to_string(count) +
                                     " expected bytes: socket error " + std::to_string(WSAGetLastError()));
        }

        received += read;
    }

    return data;
}

std::string RoundTrip(SOCKET handle, const std::string& text)
{
    SendText(handle, text);
    return ReceiveExactly(handle, static_cast<int>(text.size()));
}

void ExpectReset(SOCKET handle)
{
    std::array<char, 16> buffer{};

    // A reset is observable on the next read. A FIN (zero bytes) is a failed expectation because
    // the relay must not fall back to a graceful close.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int read = recv(handle, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (read == 0)
        {
            FAIL() << "Expected a TCP reset but the peer closed gracefully (FIN).";
        }

        if (read < 0)
        {
            const int error = WSAGetLastError();
            if (error == WSAECONNRESET || error == WSAECONNABORTED)
            {
                return;
            }

            FAIL() << "Expected a TCP reset from the relay, received socket error " << error << ".";
        }
    }

    FAIL() << "Expected a TCP reset from the relay.";
}

void WaitUntil(const std::function<bool()>& condition, const std::string& description)
{
    // 500 attempts at 10ms bound the wait at ~5s without an unbounded polling loop.
    for (int attempt = 0; attempt < c_maxWaitAttempts; ++attempt)
    {
        if (condition())
        {
            return;
        }

        std::this_thread::sleep_for(c_waitSlice);
    }

    FAIL() << "Timed out waiting for " << description << ".";
}

/// <summary>Loopback echo server: echoes bytes, records them, and answers EOF with "bye".</summary>
class EchoServer final
{
public:
    EchoServer();
    ~EchoServer();
    EchoServer(const EchoServer&) = delete;
    EchoServer& operator=(const EchoServer&) = delete;

    WslEndpoint Endpoint() const noexcept { return WslEndpoint{"127.0.0.1", m_port}; }

    std::string Received() const
    {
        std::lock_guard lock(m_gate);
        return m_received;
    }

private:
    void AcceptLoop(std::stop_token token);
    void Handle(SOCKET client);

    SOCKET m_listener = INVALID_SOCKET;
    int m_port = 0;
    std::atomic<bool> m_closed{false};
    mutable std::mutex m_gate;
    std::string m_received;
    std::vector<SOCKET> m_clients;
    std::vector<std::jthread> m_handlers;
    std::jthread m_acceptThread;
};

EchoServer::EchoServer()
{
    wslc::internal::EnsureWinsock();

    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (std::cmp_equal(listener, INVALID_SOCKET))
    {
        throw std::runtime_error("echo server socket() failed: " + std::to_string(WSAGetLastError()));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0 ||
        listen(listener, SOMAXCONN) != 0)
    {
        const int error = WSAGetLastError();
        closesocket(listener);
        throw std::runtime_error("echo server bind/listen failed: " + std::to_string(error));
    }

    int length = static_cast<int>(sizeof(address));
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        const int error = WSAGetLastError();
        closesocket(listener);
        throw std::runtime_error("echo server getsockname failed: " + std::to_string(error));
    }

    m_listener = listener;
    m_port = static_cast<int>(ntohs(address.sin_port));
    m_acceptThread = std::jthread([this](std::stop_token token) { AcceptLoop(token); });
}

EchoServer::~EchoServer()
{
    m_closed.store(true);
    m_acceptThread.request_stop();

    if (!std::cmp_equal(m_listener, INVALID_SOCKET))
    {
        closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }

    if (m_acceptThread.joinable())
    {
        m_acceptThread.join();
    }

    {
        std::lock_guard lock(m_gate);
        for (const SOCKET client : m_clients)
        {
            static_cast<void>(shutdown(client, SD_BOTH));
        }
    }

    // Handler threads are joined by their jthread destructors; each handler owns and closes its
    // socket, so teardown never closes a handle the handler may still use.
}

void EchoServer::AcceptLoop(std::stop_token token)
{
    // Bounded by c_maxEchoConnections and by the stop request, so teardown cannot leave the loop
    // spinning (ADR-0001).
    for (int accepted = 0; accepted < c_maxEchoConnections && !token.stop_requested() && !m_closed.load(); ++accepted)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_listener, &readSet);
        timeval timeoutValue{};
        timeoutValue.tv_sec = 0;
        timeoutValue.tv_usec = static_cast<long>(c_acceptSlice.count() * 1000);
        const int selected = select(0, &readSet, nullptr, nullptr, &timeoutValue);
        if (selected == 0)
        {
            continue;
        }

        if (selected < 0)
        {
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

        SetNoDelay(client);
        {
            std::lock_guard lock(m_gate);
            m_clients.push_back(client);
            m_handlers.emplace_back([this, client] { Handle(client); });
        }
    }
}

void EchoServer::Handle(SOCKET client)
{
    std::array<char, c_echoBufferSize> buffer{};

    // Bounded echo loop: at most c_maxEchoChunks reads, ending early on EOF or a socket error.
    for (int chunk = 0; chunk < c_maxEchoChunks && !m_closed.load(); ++chunk)
    {
        const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0)
        {
            break;
        }

        {
            std::lock_guard lock(m_gate);
            m_received.append(buffer.data(), static_cast<std::size_t>(received));
        }

        if (!SendAll(client, buffer.data(), received))
        {
            break;
        }
    }

    // Half-close tests rely on this marker proving the reverse direction still flows.
    static_cast<void>(SendAll(client, c_byeMarker.data(), static_cast<int>(c_byeMarker.size())));

    // Closing and unregistering under the gate keeps the destructor's shutdown() off a handle that
    // was already closed.
    std::lock_guard lock(m_gate);
    static_cast<void>(std::erase(m_clients, client));
    closesocket(client);
}

/// <summary>Client leg of a relay connection: owns the socket and applies a bounded receive timeout.</summary>
class ClientSocket final
{
public:
    explicit ClientSocket(const WslEndpoint& endpoint);
    ~ClientSocket();
    ClientSocket(const ClientSocket&) = delete;
    ClientSocket& operator=(const ClientSocket&) = delete;

    SOCKET Get() const noexcept { return m_socket; }

    void Close() noexcept;

private:
    SOCKET m_socket = INVALID_SOCKET;
};

ClientSocket::ClientSocket(const WslEndpoint& endpoint)
{
    wslc::internal::EnsureWinsock();

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(endpoint.Port));
    if (InetPtonA(AF_INET, endpoint.Host.c_str(), &address.sin_addr) != 1)
    {
        throw std::runtime_error("relay endpoint host is not an IPv4 literal: " + endpoint.Host);
    }

    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (std::cmp_equal(m_socket, INVALID_SOCKET))
    {
        throw std::runtime_error("client socket() failed: " + std::to_string(WSAGetLastError()));
    }

    if (connect(m_socket, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0)
    {
        const int error = WSAGetLastError();
        Close();
        throw std::runtime_error("client connect() failed: " + std::to_string(error));
    }

    DWORD timeout = c_socketTimeoutMs;
    static_cast<void>(setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                                 static_cast<int>(sizeof(timeout))));
    static_cast<void>(setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                                 static_cast<int>(sizeof(timeout))));
    SetNoDelay(m_socket);
}

ClientSocket::~ClientSocket()
{
    Close();
}

void ClientSocket::Close() noexcept
{
    if (!std::cmp_equal(m_socket, INVALID_SOCKET))
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

} // namespace

TEST(FaultShim, AttachBindsLoopbackEphemeralPortAndForwardsBytes)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    const WslEndpoint endpoint = shim.Endpoint();
    EXPECT_EQ(endpoint.Host, "127.0.0.1");
    EXPECT_GT(endpoint.Port, 0);
    EXPECT_NE(endpoint.Port, echo.Endpoint().Port);
    EXPECT_EQ(shim.LiveConnections(), 0);

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "ping"), "ping");

    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");
}

TEST(FaultShim, InjectResetMakesTheClientObserveAReset)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "warm"), "warm");
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    EXPECT_EQ(shim.Inject(FaultMode::Reset), 1);
    ExpectReset(client.Get());
    WaitUntil([&shim] { return shim.LiveConnections() == 0; }, "the reset connection to leave the relay");
}

TEST(FaultShim, InjectCloseSendShutsClientToBrokerWhileReverseDirectionFlows)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "open"), "open");

    EXPECT_EQ(shim.Inject(FaultMode::Close, FaultDirection::ClientToBroker), 1);

    // The echo server sees EOF on its read side and answers with the marker, which proves the
    // broker-to-client direction is still open after the half-close.
    EXPECT_EQ(ReceiveExactly(client.Get(), 3), "bye");
}

TEST(FaultShim, InjectCloseOnBrokerToClientKeepsClientToBrokerFlowing)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "open"), "open");

    EXPECT_EQ(shim.Inject(FaultMode::Close, FaultDirection::BrokerToClient), 1);

    // The client observes a clean FIN (zero bytes), not a reset.
    std::array<char, 1> buffer{};
    ASSERT_EQ(recv(client.Get(), buffer.data(), static_cast<int>(buffer.size()), 0), 0)
        << "the client must observe a clean FIN";

    // The client can still send, and the echo server still receives those bytes.
    SendText(client.Get(), "after");
    WaitUntil([&echo] { return echo.Received().find("after") != std::string::npos; },
              "the target to receive bytes after the broker-to-client half-close");
}

TEST(FaultShim, InjectStallHoldsBytesUntilClearResumesTheStream)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "warm"), "warm");

    EXPECT_EQ(shim.Inject(FaultMode::Stall, FaultDirection::BrokerToClient), 1);

    // The echo server answers "held" immediately, but the relay must hold the chunk while the
    // direction is stalled; nothing may arrive at the client in the meantime.
    SendText(client.Get(), "held");

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(client.Get(), &readSet);
    timeval timeoutValue{};
    timeoutValue.tv_sec = 0;
    timeoutValue.tv_usec = 300'000;
    EXPECT_EQ(select(0, &readSet, nullptr, nullptr, &timeoutValue), 0)
        << "the relay must hold the chunk while the direction is stalled";

    shim.Clear();

    EXPECT_EQ(ReceiveExactly(client.Get(), 4), "held");
}

TEST(FaultShim, LatencyDelaysBrokerToClientChunks)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "warm"), "warm");

    shim.Latency(200ms, FaultDirection::BrokerToClient);

    const auto started = std::chrono::steady_clock::now();
    const std::string payload = RoundTrip(client.Get(), "delay");
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    EXPECT_EQ(payload, "delay");
    EXPECT_GE(elapsed.count(), 150) << "a 200ms broker-to-client delay must be observable; the round trip took "
                                    << elapsed.count() << "ms";
}

TEST(FaultShim, ArmFaultsExactlyTheNextConnectionsThenStops)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    shim.Arm(2, FaultMode::Reset);

    for (int index = 0; index < 2; ++index)
    {
        ClientSocket resetClient(shim.Endpoint());
        ExpectReset(resetClient.Get());
    }

    ClientSocket working(shim.Endpoint());
    EXPECT_EQ(RoundTrip(working.Get(), "ok"), "ok");

    // The arm is spent; a fourth connection must not be faulted.
    ClientSocket later(shim.Endpoint());
    EXPECT_EQ(RoundTrip(later.Get(), "ok"), "ok");
}

TEST(FaultShim, InjectReturnsTheNumberOfLiveConnectionsAndZeroWhenNone)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    EXPECT_EQ(shim.Inject(FaultMode::Stall), 0);

    {
        ClientSocket first(shim.Endpoint());
        ClientSocket second(shim.Endpoint());
        EXPECT_EQ(RoundTrip(first.Get(), "one"), "one");
        EXPECT_EQ(RoundTrip(second.Get(), "two"), "two");
        WaitUntil([&shim] { return shim.LiveConnections() == 2; }, "two live relayed connections");

        EXPECT_EQ(shim.Inject(FaultMode::Stall), 2);
        shim.Clear();
        EXPECT_EQ(shim.Inject(FaultMode::Stall), 2);
        shim.Clear();
    }

    WaitUntil([&shim] { return shim.LiveConnections() == 0; }, "relayed connections to close");
    EXPECT_EQ(shim.Inject(FaultMode::Stall), 0);
}

TEST(FaultShim, ClearIsIdempotentAndDropsLatencyAndArmedFaults)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    shim.Clear();
    shim.Clear();

    shim.Arm(1, FaultMode::Reset);
    shim.Latency(5s, FaultDirection::BrokerToClient);
    shim.Clear();

    ClientSocket client(shim.Endpoint());

    // A fresh connection must neither be reset (the arm is gone) nor take five seconds (the latency
    // is gone). The bounded elapsed check fails fast if either survives.
    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(RoundTrip(client.Get(), "ok"), "ok");
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    EXPECT_LT(elapsed.count(), 2000) << "Clear must drop the armed fault and the latency";
}

TEST(FaultShim, DisposeIsIdempotentAndUseAfterDisposeThrows)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());
    ClientSocket client(shim.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "warm"), "warm");
    EXPECT_EQ(shim.Inject(FaultMode::Stall), 1);

    shim.Dispose();
    shim.Dispose();

    EXPECT_THROW(shim.Inject(FaultMode::Reset), WslException);
    EXPECT_THROW(shim.Arm(1, FaultMode::Reset), WslException);
    EXPECT_THROW(shim.Latency(1ms), WslException);
    EXPECT_THROW(shim.Clear(), WslException);
    EXPECT_THROW(shim.LiveConnections(), WslException);
    EXPECT_THROW(shim.Endpoint(), WslException);
}

TEST(FaultShim, MovedShimKeepsRelaying)
{
    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());
    auto moved = std::move(shim);

    ClientSocket client(moved.Endpoint());
    EXPECT_EQ(RoundTrip(client.Get(), "moved"), "moved");
    EXPECT_EQ(moved.Inject(FaultMode::Reset), 1);
    ExpectReset(client.Get());

    moved.Dispose();
    moved.Dispose();
}

TEST(FaultShim, InvalidArgumentsAreRejected)
{
    EXPECT_THROW(FaultShim::Attach(WslEndpoint{}), WslException);
    EXPECT_THROW(FaultShim::Attach(WslEndpoint{"127.0.0.1", 0}), WslException);
    EXPECT_THROW(FaultShim::Attach(WslEndpoint{"127.0.0.1", 70000}), WslException);

    EchoServer echo;
    auto shim = FaultShim::Attach(echo.Endpoint());

    EXPECT_THROW(shim.Arm(-1, FaultMode::Reset), WslException);
    EXPECT_THROW(shim.Latency(-1ms), WslException);
    EXPECT_THROW(shim.Inject(static_cast<FaultMode>(99)), WslException);
    EXPECT_THROW(shim.Inject(FaultMode::Reset, static_cast<FaultDirection>(99)), WslException);
    EXPECT_THROW(shim.Arm(1, static_cast<FaultMode>(99)), WslException);
}
