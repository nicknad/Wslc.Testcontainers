#include <winsock2.h>
#include <ws2tcpip.h>

#include <wil/resource.h>

#include <gtest/gtest.h>

#include "internal/tcp_http.hpp"
#include "support/integration.hpp"
#include "wslc/faults/fault_shim.hpp"
#include "wslc/modules/mosquitto.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using wslc::WslEndpoint;
using wslc::faults::FaultDirection;
using wslc::faults::FaultMode;
using wslc::faults::FaultShim;

namespace
{

// MQTT 3.1.1 control packet type prefixes: CONNECT 0x10, CONNACK 0x20, PUBLISH 0x30 (QoS 0),
// SUBSCRIBE 0x82, SUBACK 0x90, PINGREQ 0xC0, PINGRESP 0xD0.
constexpr unsigned char c_connectPacketType = 0x10;
constexpr unsigned char c_connackPacketType = 0x20;
constexpr unsigned char c_publishPacketType = 0x30;
constexpr unsigned char c_subscribePacketType = 0x82;
constexpr unsigned char c_subackPacketType = 0x90;
constexpr unsigned char c_pingReqPacketType = 0xC0;
constexpr unsigned char c_pingRespPacketType = 0xD0;

constexpr char c_sessionTopic[] = "wslc/faults/session";
constexpr char c_lastWillTopic[] = "wslc/faults/last-will";
constexpr char c_lastWillPayload[] = "wslc-client-crashed";

// Every socket step must fail in seconds so a wedged relay or broker surfaces quickly; the
// integration test itself owns the multi-minute container-start budget.
constexpr DWORD c_stepTimeoutMilliseconds = 5000;

// Keep-alive 1s plus a probe every 400ms keeps Mosquitto's 1.5x keep-alive expiry away while the
// broker-to-client direction is stalled. Two seconds without a PINGRESP is twice the keep-alive, so
// a real client would already have declared the connection dead.
constexpr std::uint16_t c_shortKeepAliveSeconds = 1;
constexpr std::chrono::milliseconds c_keepAliveProbe{400};
constexpr std::chrono::milliseconds c_stallObservation{2000};

constexpr int c_maxWaitAttempts = 500;
constexpr std::chrono::milliseconds c_waitSlice{10};

struct Will
{
    std::string_view Topic;
    std::string_view Payload;
};

struct PublishedMessage
{
    std::string Topic;
    std::string Payload;
};

void AppendLengthPrefixed(std::vector<unsigned char>& body, std::string_view value)
{
    body.push_back(static_cast<unsigned char>(value.size() >> 8));
    body.push_back(static_cast<unsigned char>(value.size() & 0xFF));
    body.insert(body.end(), value.begin(), value.end());
}

/// <summary>Builds a packet: fixed-header type byte, MQTT variable-length size, then the body.</summary>
std::vector<unsigned char> Packet(unsigned char header, std::span<const unsigned char> body)
{
    std::vector<unsigned char> packet{header};
    std::size_t remaining = body.size();
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while): an empty body still needs one length byte.
    do
    {
        unsigned char encoded = static_cast<unsigned char>(remaining % 128);
        remaining /= 128;
        if (remaining > 0)
        {
            encoded = static_cast<unsigned char>(encoded | 0x80);
        }

        packet.push_back(encoded);
    } while (remaining > 0);

    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

std::vector<unsigned char> ConnectPacket(std::string_view clientId, std::uint16_t keepAliveSeconds = 60,
                                         std::optional<Will> will = std::nullopt)
{
    std::vector<unsigned char> body = {
        0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, // Protocol name "MQTT".
        0x04,                               // Protocol level 4 (MQTT 3.1.1).
    };
    // Clean session plus the will flag when a will is supplied: Will QoS 0, no retain.
    body.push_back(static_cast<unsigned char>(will.has_value() ? 0x06 : 0x02));
    body.push_back(static_cast<unsigned char>(keepAliveSeconds >> 8));
    body.push_back(static_cast<unsigned char>(keepAliveSeconds & 0xFF));
    AppendLengthPrefixed(body, clientId);

    if (will.has_value())
    {
        AppendLengthPrefixed(body, will->Topic);
        AppendLengthPrefixed(body, will->Payload);
    }

    return Packet(c_connectPacketType, body);
}

std::vector<unsigned char> SubscribePacket(std::uint16_t packetId, std::string_view topicFilter, unsigned char qos)
{
    std::vector<unsigned char> body;
    body.push_back(static_cast<unsigned char>(packetId >> 8));
    body.push_back(static_cast<unsigned char>(packetId & 0xFF));
    AppendLengthPrefixed(body, topicFilter);
    body.push_back(qos);
    return Packet(c_subscribePacketType, body);
}

std::vector<unsigned char> PublishPacket(std::string_view topic, std::string_view payload)
{
    std::vector<unsigned char> body;
    AppendLengthPrefixed(body, topic);
    body.insert(body.end(), payload.begin(), payload.end());
    return Packet(c_publishPacketType, body);
}

std::vector<unsigned char> PingReqPacket()
{
    return {c_pingReqPacketType, 0x00};
}

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

bool SendAll(SOCKET socket, std::span<const unsigned char> bytes)
{
    std::size_t sent = 0;
    while (sent < bytes.size())
    {
        const int written =
            send(socket, reinterpret_cast<const char*>(bytes.data() + sent), static_cast<int>(bytes.size() - sent), 0);
        if (written <= 0)
        {
            return false;
        }

        sent += static_cast<std::size_t>(written);
    }

    return true;
}

bool ReceivePacket(SOCKET socket, unsigned char& header, std::vector<unsigned char>& body)
{
    unsigned char fixedHeader = 0;
    if (recv(socket, reinterpret_cast<char*>(&fixedHeader), 1, MSG_WAITALL) != 1)
    {
        return false;
    }

    std::size_t remaining = 0;
    std::size_t shift = 0;
    for (;;)
    {
        unsigned char lengthByte = 0;
        if (recv(socket, reinterpret_cast<char*>(&lengthByte), 1, MSG_WAITALL) != 1)
        {
            return false;
        }

        remaining |= static_cast<std::size_t>(lengthByte & 0x7F) << shift;
        shift += 7;
        if ((lengthByte & 0x80) == 0)
        {
            break;
        }

        // MQTT remaining length is encoded in at most four bytes.
        if (shift > 21u)
        {
            return false;
        }
    }

    body.assign(remaining, 0);
    if (remaining > 0 && recv(socket, reinterpret_cast<char*>(body.data()), static_cast<int>(remaining), MSG_WAITALL) !=
                             static_cast<int>(remaining))
    {
        return false;
    }

    header = fixedHeader;
    return true;
}

std::string AsString(std::span<const unsigned char> bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

PublishedMessage ParsePublish(std::span<const unsigned char> body)
{
    const std::size_t topicLength = (static_cast<std::size_t>(body[0]) << 8) | static_cast<std::size_t>(body[1]);
    return {AsString(body.subspan(2, topicLength)), AsString(body.subspan(2 + topicLength))};
}

/// <summary>Connects, sends CONNECT and validates the CONNACK accepted the client.</summary>
bool ConnectClient(SOCKET socket, std::string_view clientId, std::uint16_t keepAliveSeconds = 60,
                   std::optional<Will> will = std::nullopt)
{
    if (!SendAll(socket, ConnectPacket(clientId, keepAliveSeconds, will)))
    {
        return false;
    }

    unsigned char header = 0;
    std::vector<unsigned char> body;
    if (!ReceivePacket(socket, header, body) || header != c_connackPacketType || body.size() != 2u)
    {
        return false;
    }

    // Session-present flag 0, connect return code 0 (accepted).
    return body[1] == 0;
}

bool Subscribe(SOCKET socket, std::uint16_t packetId, std::string_view topicFilter)
{
    if (!SendAll(socket, SubscribePacket(packetId, topicFilter, 0)))
    {
        return false;
    }

    unsigned char header = 0;
    std::vector<unsigned char> body;
    return ReceivePacket(socket, header, body) && header == c_subackPacketType &&
           body == (std::vector<unsigned char>{static_cast<unsigned char>(packetId >> 8),
                                               static_cast<unsigned char>(packetId & 0xFF), 0x00});
}

/// <summary>Reports whether the next reads observe a TCP reset rather than a graceful FIN.</summary>
/// <remarks>
/// A reset is observable on the next read; a FIN (zero bytes) or a receive timeout is a failed
/// expectation because the relay must not fall back to a graceful close.
/// </remarks>
bool ObservesReset(SOCKET socket)
{
    std::array<char, 64> buffer{};

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

/// <summary>Connects to the endpoint and reports whether the connection failed at socket level.</summary>
bool ObservesReset(const WslEndpoint& endpoint)
{
    const wil::unique_socket handle = ConnectTo(endpoint);
    if (!handle)
    {
        // The reset may also arrive before the handshake finishes; that is still a socket-level
        // failure rather than a protocol rejection.
        return true;
    }

    return ObservesReset(handle.get());
}

void ExpectReset(SOCKET socket)
{
    ASSERT_TRUE(ObservesReset(socket)) << "Expected a TCP reset from the relay but the peer closed gracefully (FIN).";
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

TEST(MosquittoFault, ConnectsThroughTheRelayAndCompletesASession)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    auto shim = FaultShim::Attach(mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort));
    const WslEndpoint relayed = shim.Endpoint();

    EXPECT_EQ(mosquitto.GetConnectionString(relayed), "mqtt://127.0.0.1:" + std::to_string(relayed.Port));

    const wil::unique_socket client = ConnectTo(relayed);
    ASSERT_TRUE(client);
    ASSERT_TRUE(ConnectClient(client.get(), "wslc-relay-session"));
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_TRUE(Subscribe(client.get(), 1, c_sessionTopic));
    ASSERT_TRUE(SendAll(client.get(), PublishPacket(c_sessionTopic, "hello through the relay")));

    unsigned char header = 0;
    std::vector<unsigned char> body;
    ASSERT_TRUE(ReceivePacket(client.get(), header, body));
    ASSERT_EQ(header, c_publishPacketType);
    ASSERT_GE(body.size(), 2u);
    const PublishedMessage published = ParsePublish(body);
    EXPECT_EQ(published.Topic, c_sessionTopic);
    EXPECT_EQ(published.Payload, "hello through the relay");
}

TEST(MosquittoFault, ResetDropsTheClientAndTheSubscriberReceivesItsLastWill)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    const WslEndpoint direct = mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort);
    auto shim = FaultShim::Attach(direct);

    const wil::unique_socket subscriber = ConnectTo(direct);
    ASSERT_TRUE(subscriber);
    ASSERT_TRUE(ConnectClient(subscriber.get(), "wslc-will-subscriber"));
    ASSERT_TRUE(Subscribe(subscriber.get(), 1, c_lastWillTopic));

    const wil::unique_socket client = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(client);
    ASSERT_TRUE(ConnectClient(client.get(), "wslc-will-client", 60, Will{c_lastWillTopic, c_lastWillPayload}));
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_EQ(shim.Inject(FaultMode::Reset), 1);

    ExpectReset(client.get());

    // The broker saw an ungraceful reset, so it publishes the will of the dropped client.
    unsigned char header = 0;
    std::vector<unsigned char> body;
    ASSERT_TRUE(ReceivePacket(subscriber.get(), header, body));
    ASSERT_EQ(header, c_publishPacketType);
    ASSERT_GE(body.size(), 2u);
    const PublishedMessage will = ParsePublish(body);
    EXPECT_EQ(will.Topic, c_lastWillTopic);
    EXPECT_EQ(will.Payload, c_lastWillPayload);
}

TEST(MosquittoFault, StallOnBrokerToClientTimesOutTheClientWhileTheBrokerStaysAlive)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    const WslEndpoint direct = mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort);
    auto shim = FaultShim::Attach(direct);

    const wil::unique_socket stalled = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(stalled);
    ASSERT_TRUE(ConnectClient(stalled.get(), "wslc-stalled-client", c_shortKeepAliveSeconds));
    WaitUntil([&shim] { return shim.LiveConnections() == 1; }, "one live relayed connection");

    ASSERT_EQ(shim.Inject(FaultMode::Stall, FaultDirection::BrokerToClient), 1);

    // Model a client's keep-alive: keep sending PINGREQ so the broker's own 1.5x timer never fires,
    // and assert that no PINGRESP makes it back within twice the keep-alive interval.
    const auto started = std::chrono::steady_clock::now();
    int probes = 0;
    while (std::chrono::steady_clock::now() - started < c_stallObservation)
    {
        ASSERT_TRUE(SendAll(stalled.get(), PingReqPacket()));
        ++probes;

        if (IsReadable(stalled.get(), c_keepAliveProbe))
        {
            unsigned char header = 0;
            std::vector<unsigned char> body;
            ASSERT_TRUE(ReceivePacket(stalled.get(), header, body));
            FAIL() << "A stalled broker-to-client direction delivered MQTT packet 0x" << std::hex
                   << static_cast<int>(header) << std::dec << ".";
        }
    }

    EXPECT_GT(probes, 0);

    // The stall is directional: the socket stays open and the broker serves other clients.
    EXPECT_EQ(shim.LiveConnections(), 1);
    const wil::unique_socket directClient = ConnectTo(direct);
    ASSERT_TRUE(directClient);
    ASSERT_TRUE(ConnectClient(directClient.get(), "wslc-direct-while-stalled"));
    ASSERT_TRUE(Subscribe(directClient.get(), 1, c_sessionTopic));

    // Clear resumes the held bytes instead of dropping them.
    shim.Clear();
    unsigned char header = 0;
    std::vector<unsigned char> body;
    ASSERT_TRUE(ReceivePacket(stalled.get(), header, body));
    EXPECT_EQ(header, c_pingRespPacketType);
}

TEST(MosquittoFault, ArmResetsTheNextTwoConnectionsAndTheThirdSucceeds)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    auto shim = FaultShim::Attach(mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort));
    const WslEndpoint relayed = shim.Endpoint();

    shim.Arm(2, FaultMode::Reset);

    // The armed fault applies at accept time, so no MQTT bytes are needed to prove the socket itself
    // fails; a graceful FIN would be a contract violation.
    EXPECT_TRUE(ObservesReset(relayed)) << "The first armed connection should have been reset at the socket level.";
    EXPECT_TRUE(ObservesReset(relayed)) << "The second armed connection should have been reset at the socket level.";

    const wil::unique_socket third = ConnectTo(relayed);
    ASSERT_TRUE(third);
    ASSERT_TRUE(ConnectClient(third.get(), "wslc-arm-third"));
    ASSERT_TRUE(Subscribe(third.get(), 1, c_sessionTopic));
}

TEST(MosquittoFault, LatencyOnBrokerToClientDelaysTheConnackWithoutBreakingTheSession)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    auto shim = FaultShim::Attach(mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort));

    shim.Latency(400ms, FaultDirection::BrokerToClient);

    const wil::unique_socket client = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(client);

    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(ConnectClient(client.get(), "wslc-latency"));
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    EXPECT_GE(elapsed.count(), 300) << "A 400ms broker-to-client delay must be observable on CONNACK; it took "
                                    << elapsed.count() << "ms.";

    // The delayed session still works: SUBACK arrives after the session is usable again.
    ASSERT_TRUE(Subscribe(client.get(), 1, c_sessionTopic));
}

TEST(MosquittoFault, ClearRestoresNormalTrafficForAFreshConnection)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    auto shim = FaultShim::Attach(mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort));

    shim.Latency(10s, FaultDirection::BrokerToClient);
    shim.Arm(5, FaultMode::Reset);
    shim.Clear();

    const auto started = std::chrono::steady_clock::now();
    const wil::unique_socket client = ConnectTo(shim.Endpoint());
    ASSERT_TRUE(client);
    ASSERT_TRUE(ConnectClient(client.get(), "wslc-cleared"));
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    EXPECT_LT(elapsed.count(), 2000) << "Clear must drop latency and armed faults; the session took " << elapsed.count()
                                     << "ms.";
    ASSERT_TRUE(Subscribe(client.get(), 1, c_sessionTopic));
}

TEST(MosquittoFault, RelayDisposesBeforeTheContainerAndTheDirectEndpointKeepsWorking)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    wslc::internal::EnsureWinsock();

    auto mosquitto = wslc::modules::MosquittoBuilder{}.Build();
    mosquitto.Start();
    const WslEndpoint direct = mosquitto.GetConnectEndpoint(wslc::modules::MosquittoContainer::DefaultPort);
    auto shim = FaultShim::Attach(direct);

    {
        const wil::unique_socket relayed = ConnectTo(shim.Endpoint());
        ASSERT_TRUE(relayed);
        ASSERT_TRUE(ConnectClient(relayed.get(), "wslc-relayed"));
        ASSERT_TRUE(Subscribe(relayed.get(), 1, c_sessionTopic));
    }

    // Attaching the relay must not affect the direct endpoint used by readiness/triage.
    {
        const wil::unique_socket directClient = ConnectTo(direct);
        ASSERT_TRUE(directClient);
        ASSERT_TRUE(ConnectClient(directClient.get(), "wslc-direct"));
        ASSERT_TRUE(Subscribe(directClient.get(), 1, c_sessionTopic));
    }

    shim.Dispose();

    // The broker is untouched by the relay's teardown, so the direct path still works.
    {
        const wil::unique_socket afterRelay = ConnectTo(direct);
        ASSERT_TRUE(afterRelay);
        ASSERT_TRUE(ConnectClient(afterRelay.get(), "wslc-after-relay"));
        ASSERT_TRUE(Subscribe(afterRelay.get(), 1, c_sessionTopic));
    }

    mosquitto.Dispose();
    EXPECT_FALSE(mosquitto.IsStarted());
}
