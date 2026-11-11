#include <winsock2.h>
#include <ws2tcpip.h>

#include <wil/resource.h>

#include <gtest/gtest.h>

#include "support/integration.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/modules/artemis.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// MQTT 3.1.1 control packet type prefixes: CONNECT 0x10, CONNACK 0x20, PUBLISH 0x30
// (QoS 0), SUBSCRIBE 0x82, SUBACK 0x90.
constexpr unsigned char c_connectPacketType = 0x10;
constexpr unsigned char c_connackPacketType = 0x20;
constexpr unsigned char c_publishPacketType = 0x30;
constexpr unsigned char c_subscribePacketType = 0x82;
constexpr unsigned char c_subackPacketType = 0x90;

// Bounds the socket reads so a wedged broker fails the test instead of hanging it.
constexpr DWORD c_receiveTimeoutMilliseconds = 30000;

/// <summary>Owns the Winsock startup/shutdown pair for the raw-socket integration test.</summary>
class WinsockSession
{
public:
    WinsockSession() : m_started(WSAStartup(MAKEWORD(2, 2), &m_data) == 0) {}

    ~WinsockSession()
    {
        if (m_started)
        {
            WSACleanup();
        }
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    bool Started() const { return m_started; }

private:
    WSADATA m_data{};
    bool m_started;
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

std::vector<unsigned char> ConnectPacket(std::string_view clientId)
{
    std::vector<unsigned char> body = {
        0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, // Protocol name "MQTT".
        0x04,                               // Protocol level 4 (MQTT 3.1.1).
        0x02,                               // Clean session.
        0x00, 0x3C,                         // Keep-alive 60 seconds.
    };
    AppendLengthPrefixed(body, clientId);
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

/// <summary>Connects a blocking IPv4 client socket to the mapped endpoint, or returns a null handle.</summary>
wil::unique_socket ConnectTo(wslc::WslEndpoint endpoint)
{
    wil::unique_socket handle(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!handle)
    {
        return handle;
    }

    setsockopt(handle.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&c_receiveTimeoutMilliseconds),
               sizeof(c_receiveTimeoutMilliseconds));

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

/// <summary>Connects, sends CONNECT and validates the CONNACK accepted the client.</summary>
bool ConnectClient(SOCKET socket, std::string_view clientId)
{
    if (!SendAll(socket, ConnectPacket(clientId)))
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

} // namespace

TEST(ArtemisModule, BuildsWithTheDefaultImageAndPort)
{
    auto container = wslc::modules::ArtemisBuilder{}.Build();

    ASSERT_TRUE(container.Image().has_value());
    EXPECT_EQ(*container.Image(), "docker.io/apache/artemis:2.57.0-alpine");
    EXPECT_EQ(wslc::modules::ArtemisContainer::DefaultPort, 1883);
    EXPECT_FALSE(container.IsStarted());
}

TEST(ArtemisModule, BuilderChainsAndHonorsTheImageOverride)
{
    wslc::modules::ArtemisBuilder builder;

    EXPECT_EQ(&builder.WithImage("docker.io/apache/artemis:2.56.0-alpine"), &builder);
    EXPECT_EQ(&builder.WithWaitTimeout(std::chrono::seconds(30)), &builder);
    EXPECT_EQ(&builder.WithReuse(), &builder);
    EXPECT_EQ(&builder.WithCredentials("user", "secret"), &builder);
    EXPECT_THROW(builder.WithCredentials(" ", "secret"), wslc::WslException);

    auto container = builder.Build();
    ASSERT_TRUE(container.Image().has_value());
    EXPECT_EQ(*container.Image(), "docker.io/apache/artemis:2.56.0-alpine");
}

TEST(ArtemisModule, RendersTheConnectionStringForASuppliedEndpoint)
{
    const auto anonymous = wslc::modules::ArtemisBuilder{}.Build();

    EXPECT_EQ(anonymous.GetConnectionString(wslc::WslEndpoint{"127.0.0.1", 49153}), "mqtt://127.0.0.1:49153");
    EXPECT_EQ(anonymous.GetConnectionString(wslc::WslEndpoint{"::1", 49153}), "mqtt://[::1]:49153");

    wslc::modules::ArtemisBuilder builder;
    builder.WithCredentials("user", "p@ss word");
    const auto secured = builder.Build();

    EXPECT_EQ(secured.GetConnectionString(wslc::WslEndpoint{"127.0.0.1", 49153}),
              "mqtt://user:p%40ss%20word@127.0.0.1:49153");
}

TEST(IntegrationModules, ArtemisModuleRoutesAPublishBetweenConnections)
{
    WSLC_SKIP_UNLESS_INTEGRATION();

    const WinsockSession winsock;
    ASSERT_TRUE(winsock.Started());

    wslc::modules::ArtemisBuilder builder;
    auto artemis = builder.Build();
    try
    {
        artemis.Start();

        EXPECT_EQ(artemis.GetConnectionString().rfind("mqtt://127.0.0.1:", 0), 0u);
        const wslc::WslEndpoint endpoint = artemis.GetConnectEndpoint(wslc::modules::ArtemisContainer::DefaultPort);

        wil::unique_socket subscriber = ConnectTo(endpoint);
        ASSERT_TRUE(subscriber);
        ASSERT_TRUE(ConnectClient(subscriber.get(), "wslc-artemis-subscriber"));

        ASSERT_TRUE(SendAll(subscriber.get(), SubscribePacket(1, "wslc/artemis", 0)));
        unsigned char subackHeader = 0;
        std::vector<unsigned char> subackBody;
        ASSERT_TRUE(ReceivePacket(subscriber.get(), subackHeader, subackBody));
        EXPECT_EQ(subackHeader, c_subackPacketType);
        // Packet id 1, granted QoS 0.
        EXPECT_EQ(subackBody, (std::vector<unsigned char>{0x00, 0x01, 0x00}));

        wil::unique_socket publisher = ConnectTo(endpoint);
        ASSERT_TRUE(publisher);
        ASSERT_TRUE(ConnectClient(publisher.get(), "wslc-artemis-publisher"));

        ASSERT_TRUE(SendAll(publisher.get(), PublishPacket("wslc/artemis", "hello")));

        unsigned char publishHeader = 0;
        std::vector<unsigned char> publishBody;
        ASSERT_TRUE(ReceivePacket(subscriber.get(), publishHeader, publishBody));
        EXPECT_EQ(publishHeader, c_publishPacketType);
        ASSERT_GE(publishBody.size(), 2u);
        const std::size_t topicLength =
            (static_cast<std::size_t>(publishBody[0]) << 8) | static_cast<std::size_t>(publishBody[1]);
        ASSERT_GE(publishBody.size(), 2 + topicLength);
        EXPECT_EQ(AsString(std::span(publishBody).subspan(2, topicLength)), "wslc/artemis");
        EXPECT_EQ(AsString(std::span(publishBody).subspan(2 + topicLength)), "hello");
    }
    catch (...)
    {
        artemis.Dispose();
        throw;
    }

    artemis.Dispose();
}
