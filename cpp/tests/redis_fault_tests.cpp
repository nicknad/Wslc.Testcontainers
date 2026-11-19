#include <winsock2.h>
#include <ws2tcpip.h>

#include <wil/resource.h>

#include <gtest/gtest.h>

#include "internal/tcp_http.hpp"
#include "support/integration.hpp"
#include "wslc/faults/fault_shim.hpp"
#include "wslc/modules/redis.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

using wslc::WslEndpoint;
using wslc::faults::FaultDirection;
using wslc::faults::FaultMode;
using wslc::faults::FaultShim;

namespace
{

// Every socket step must fail in seconds so a wedged relay or Redis instance surfaces quickly; the
// integration test itself owns the multi-minute container-start budget.
constexpr DWORD c_stepTimeoutMilliseconds = 5000;

// A stalled direction must keep the client silent for this window: a healthy loopback reply
// arrives in milliseconds, so half a second is a safe bound that still fails a forwarding leak.
constexpr std::chrono::milliseconds c_stallObservation{500};

constexpr int c_maxWaitAttempts = 500;
constexpr std::chrono::milliseconds c_waitSlice{10};

// Per ADR bounded loops a reply line or bulk payload never legitimately reaches this cap in this
// test; the bound only stops a wedged or hostile peer from streaming unbounded bytes into memory.
constexpr int c_maxReplyBytes = 512;

/// <summary>One parsed RESP reply: the type marker plus its payload without the line terminator.</summary>
struct RedisReply
{
    char Marker;
    std::string Payload;
};

/// <summary>Connects a blocking IPv4 client socket to the endpoint, or returns a null handle.</summary>
wil::unique_socket ConnectTo(const WslEndpoint& endpoint)
{
    wil::unique_socket handle(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!handle)
    {
        return handle;
    }

    static_cast<void>(setsockopt(handle.get(), SOL_SOCKET, SO_RCVTIMEO,
                                 reinterpret_cast<const char*>(&c_stepTimeoutMilliseconds),
                                 sizeof(c_stepTimeoutMilliseconds)));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(endpoint.Port));
    if (InetPtonA(AF_INET, endpoint.Host.c_str(), &address.sin_addr) != 1 ||
        ::connect(handle.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        return {};
    }

    return handle;
}

bool SendAll(SOCKET socket, std::span<const char> bytes)
{
    std::size_t sent = 0;
    // Bounded by the payload: every successful send advances the offset by at least one byte.
    while (sent < bytes.size())
    {
        const int written = send(socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
        if (written <= 0)
        {
            return false;
        }

        sent += static_cast<std::size_t>(written);
    }

    return true;
}

/// <summary>Encodes the arguments as one RESP array command and sends it.</summary>
bool SendCommand(SOCKET socket, std::initializer_list<std::string_view> arguments)
{
    std::string encoded = "*" + std::to_string(arguments.size()) + "\r\n";
    for (const std::string_view argument : arguments)
    {
        encoded += "$" + std::to_string(argument.size()) + "\r\n";
        encoded.append(argument);
        encoded += "\r\n";
    }

    return SendAll(socket, std::span<const char>{encoded});
}

/// <summary>Reads one CRLF-terminated RESP line without its terminator.</summary>
std::optional<std::string> ReceiveLine(SOCKET socket)
{
    std::string line;
    // Bounded per ADR bounded loops: a reply line in this test never legitimately reaches the cap.
    while (line.size() < static_cast<std::size_t>(c_maxReplyBytes))
    {
        char next = 0;
        if (recv(socket, &next, 1, MSG_WAITALL) != 1)
        {
            return std::nullopt;
        }

        if (next == '\n' && !line.empty() && line.back() == '\r')
        {
            line.pop_back();
            return line;
        }

        line.push_back(next);
    }

    return std::nullopt;
}

/// <summary>Reads one complete RESP reply; a null bulk reply carries an empty payload.</summary>
std::optional<RedisReply> ReceiveReply(SOCKET socket)
{
    char marker = 0;
    if (recv(socket, &marker, 1, MSG_WAITALL) != 1)
    {
        return std::nullopt;
    }

    const std::optional<std::string> header = ReceiveLine(socket);
    if (!header.has_value())
    {
        return std::nullopt;
    }

    if (marker != '$')
    {
        return RedisReply{marker, *header};
    }

    // Bulk strings carry their exact length; anything unparsable or oversized fails the read.
    int length = 0;
    const std::from_chars_result parsed = std::from_chars(header->data(), header->data() + header->size(), length);
    if (parsed.ec != std::errc{} || parsed.ptr != header->data() + header->size() || length < -1 ||
        length > c_maxReplyBytes)
    {
        return std::nullopt;
    }

    if (length < 0)
    {
        // Null bulk: no payload bytes follow the header.
        return RedisReply{marker, std::string{}};
    }

    std::string payload(static_cast<std::size_t>(length), '\0');
    if (length > 0 && recv(socket, payload.data(), length, MSG_WAITALL) != length)
    {
        return std::nullopt;
    }

    char tail[2]{};
    if (recv(socket, tail, 2, MSG_WAITALL) != 2 || tail[0] != '\r' || tail[1] != '\n')
    {
        return std::nullopt;
    }

    return RedisReply{marker, payload};
}

/// <summary>Reports whether the next reads observe a TCP reset rather than a graceful FIN.</summary>
/// <remarks>
/// A reset is observable on the next read; a FIN (zero bytes) or a receive timeout fails the
/// expectation because the relay must not fall back to a graceful close.
/// </remarks>
bool ObservesReset(SOCKET socket)
{
    std::array<char, 64> buffer{};
    // Bounded per ADR bounded loops: a reset surfaces on the first failing read.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int read = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (read == 0)
        {
            return false;
        }

        if (read < 0)
        {
            const int error = WSAGetLastError();
            return error == WSAECONNRESET || error == WSAECONNABORTED;
        }
    }

    return false;
}

/// <summary>Reports whether the socket has readable data within the timeout; performs no read.</summary>
bool IsReadable(SOCKET socket, std::chrono::milliseconds timeout)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);
    timeval timeoutValue{};
    timeoutValue.tv_sec = static_cast<long>(timeout.count() / 1000);
    timeoutValue.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    return select(0, &readSet, nullptr, nullptr, &timeoutValue) > 0;
}

void WaitUntil(const std::function<bool()>& condition, std::string_view description)
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

} // namespace

TEST(RedisFault, SetAndGetRoundTripThroughTheRelay)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto redis = wslc::modules::RedisBuilder{}.Build();
    redis.Start();
    const WslEndpoint direct = redis.GetConnectEndpoint(wslc::modules::RedisContainer::DefaultPort);
    auto shim = FaultShim::Attach(direct);
    const WslEndpoint relayed = shim.Endpoint();

    EXPECT_EQ(relayed.Host, "127.0.0.1");
    EXPECT_GT(relayed.Port, 0);
    EXPECT_NE(relayed.Port, direct.Port);

    const wil::unique_socket client = ConnectTo(relayed);
    ASSERT_TRUE(client);
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_TRUE(SendCommand(client.get(), {"SET", "wslc:faults:session", "hello-through-the-relay"}));
    auto setReply = ReceiveReply(client.get());
    ASSERT_TRUE(setReply.has_value());
    EXPECT_EQ(setReply->Marker, '+');
    EXPECT_EQ(setReply->Payload, "OK");

    ASSERT_TRUE(SendCommand(client.get(), {"GET", "wslc:faults:session"}));
    auto getReply = ReceiveReply(client.get());
    ASSERT_TRUE(getReply.has_value());
    EXPECT_EQ(getReply->Marker, '$');
    EXPECT_EQ(getReply->Payload, "hello-through-the-relay");
}

TEST(RedisFault, InjectResetDropsTheClientAtTheSocketLevel)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto redis = wslc::modules::RedisBuilder{}.Build();
    redis.Start();
    auto shim = FaultShim::Attach(redis.GetConnectEndpoint(wslc::modules::RedisContainer::DefaultPort));

    const wil::unique_socket client = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(client);
    ASSERT_TRUE(SendCommand(client.get(), {"PING"}));
    auto warm = ReceiveReply(client.get());
    ASSERT_TRUE(warm.has_value());
    EXPECT_EQ(warm->Payload, "PONG");
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_EQ(shim.Inject(FaultMode::Reset), 1);

    // The relay aborts both legs, so the client must observe RST rather than a graceful FIN.
    EXPECT_TRUE(ObservesReset(client.get())) << "Expected a TCP reset from the relay but the peer closed gracefully.";
    WaitUntil([&shim] { return shim.LiveConnections() == 0; }, "the reset connection to leave the relay");
}

TEST(RedisFault, ArmedStallSilencesTheNextConnectionUntilClearResumesIt)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto redis = wslc::modules::RedisBuilder{}.Build();
    redis.Start();
    auto shim = FaultShim::Attach(redis.GetConnectEndpoint(wslc::modules::RedisContainer::DefaultPort));

    // Seed the value over a normal connection first so the held request has a real answer.
    {
        const wil::unique_socket seeder = ConnectTo(shim.Endpoint());
        ASSERT_TRUE(seeder);
        ASSERT_TRUE(SendCommand(seeder.get(), {"SET", "wslc:faults:stall", "held-value"}));
        auto reply = ReceiveReply(seeder.get());
        ASSERT_TRUE(reply.has_value());
        EXPECT_EQ(reply->Payload, "OK");
    }
    WaitUntil([&shim] { return shim.LiveConnections() == 0; }, "the seeding connection to close");

    shim.Arm(1, FaultMode::Stall);

    const wil::unique_socket stalled = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(stalled);
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "the armed connection to be relayed");

    // The armed stall applies at accept time, so the request is held silently by the relay.
    ASSERT_TRUE(SendCommand(stalled.get(), {"GET", "wslc:faults:stall"}));
    EXPECT_FALSE(IsReadable(stalled.get(), c_stallObservation)) << "the armed stall must hold the request silently";

    // Clear resumes the held bytes instead of dropping them, so the request completes late.
    shim.Clear();

    auto reply = ReceiveReply(stalled.get());
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->Marker, '$');
    EXPECT_EQ(reply->Payload, "held-value");
}

TEST(RedisFault, StallOnTargetToClientHoldsRepliesWhileRequestsKeepFlowing)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto redis = wslc::modules::RedisBuilder{}.Build();
    redis.Start();
    auto shim = FaultShim::Attach(redis.GetConnectEndpoint(wslc::modules::RedisContainer::DefaultPort));

    const wil::unique_socket client = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(client);
    ASSERT_TRUE(SendCommand(client.get(), {"PING"}));
    auto warm = ReceiveReply(client.get());
    ASSERT_TRUE(warm.has_value());
    EXPECT_EQ(warm->Payload, "PONG");
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_EQ(shim.Inject(FaultMode::Stall, FaultDirection::TargetToClient), 1);

    // The client-to-target direction still flows: the server processes both increments while every
    // reply is held back by the stalled direction.
    ASSERT_TRUE(SendCommand(client.get(), {"INCR", "wslc:faults:counter"}));
    ASSERT_TRUE(SendCommand(client.get(), {"INCR", "wslc:faults:counter"}));

    EXPECT_FALSE(IsReadable(client.get(), c_stallObservation))
        << "a stalled target-to-client direction must hold every reply";
    EXPECT_EQ(shim.LiveConnections(), 1);

    // Clear resumes the held replies in order, so both increments answer with their real values.
    shim.Clear();

    auto first = ReceiveReply(client.get());
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->Marker, ':');
    EXPECT_EQ(first->Payload, "1");
    auto second = ReceiveReply(client.get());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->Marker, ':');
    EXPECT_EQ(second->Payload, "2");
}
