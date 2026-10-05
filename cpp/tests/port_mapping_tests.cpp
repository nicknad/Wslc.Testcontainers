#include <gtest/gtest.h>

#include "internal/port_mapping.hpp"
#include "wslc/exceptions.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using wslc::WslNetworkException;
using wslc::internal::PortMapping;
using wslc::internal::WslPortMappingRecord;

namespace
{

WslPortMappingRecord Port(int port, std::optional<std::string> bindAddress = std::nullopt)
{
    return WslPortMappingRecord{port, std::move(bindAddress)};
}

std::string FormatAddress(const sockaddr* address)
{
    if (address == nullptr)
    {
        return {};
    }

    char buffer[INET6_ADDRSTRLEN] = {};
    if (address->sa_family == AF_INET)
    {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        InetNtopA(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer));
        return buffer;
    }

    if (address->sa_family == AF_INET6)
    {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        InetNtopA(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer));
        return buffer;
    }

    return {};
}

void ThrowIfFailed(int result)
{
    if (result != 0)
    {
        throw std::runtime_error("Loopback listener socket operation failed.");
    }
}

class LoopbackListener
{
public:
    explicit LoopbackListener(int family) : m_socket(CreateSocket(family))
    {
        if (family == AF_INET)
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ThrowIfFailed(bind(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
            int length = sizeof(address);
            ThrowIfFailed(getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length));
            m_port = ntohs(address.sin_port);
        }
        else
        {
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_addr = in6addr_loopback;
            ThrowIfFailed(bind(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
            int length = sizeof(address);
            ThrowIfFailed(getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length));
            m_port = ntohs(address.sin6_port);
        }

        ThrowIfFailed(listen(m_socket, 1));
    }

    ~LoopbackListener()
    {
        if (m_socket != std::numeric_limits<SOCKET>::max())
        {
            closesocket(m_socket);
        }
    }

    int Port() const noexcept { return m_port; }

private:
    static SOCKET CreateSocket(int family)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("WSAStartup failed.");
        }

        return socket(family, SOCK_STREAM, IPPROTO_TCP);
    }

    SOCKET m_socket = INVALID_SOCKET;
    int m_port = 0;
};

std::string InspectPayload(int port)
{
    return "{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + std::to_string(port) + "\"}]}}";
}

} // namespace

TEST(PortMapping, MappingsAreDynamicUntilResolved)
{
    const PortMapping mapping = PortMapping::Create({Port(5432), Port(8080), Port(5432)});

    EXPECT_THROW(mapping.GetMappedPort(5432), WslNetworkException);

    std::vector<sockaddr_storage> storage;
    const std::vector<WslcContainerPortMapping> mappings = mapping.ToNativeMappings(storage);
    ASSERT_EQ(mappings.size(), 2u);
    for (const auto& item : mappings)
    {
        EXPECT_EQ(static_cast<int>(item.windowsPort), 0);
        EXPECT_EQ(static_cast<int>(item.protocol), static_cast<int>(WSLC_PORT_PROTOCOL_TCP));
    }
}

TEST(PortMapping, BindAddressIsForwardedToTheContainerMapping)
{
    const PortMapping mapping = PortMapping::Create({Port(8080, "127.0.0.1"), Port(9090)});

    std::vector<sockaddr_storage> storage;
    const std::vector<WslcContainerPortMapping> mappings = mapping.ToNativeMappings(storage);

    const WslcContainerPortMapping* bound = nullptr;
    const WslcContainerPortMapping* unbound = nullptr;
    for (const auto& item : mappings)
    {
        if (item.containerPort == 8080)
        {
            bound = &item;
        }
        else if (item.containerPort == 9090)
        {
            unbound = &item;
        }
    }

    ASSERT_NE(bound, nullptr);
    ASSERT_NE(unbound, nullptr);
    ASSERT_NE(bound->windowsAddress, nullptr);
    EXPECT_EQ(FormatAddress(reinterpret_cast<const sockaddr*>(bound->windowsAddress)), std::string("127.0.0.1"));
    EXPECT_EQ(unbound->windowsAddress, nullptr);
}

TEST(PortMapping, ProbeHostFollowsTheBindAddress)
{
    EXPECT_EQ(PortMapping::Create({Port(8080)}).GetProbeHost(8080), std::string("127.0.0.1"));
    EXPECT_EQ(PortMapping::Create({Port(8080, "0.0.0.0")}).GetProbeHost(8080), std::string("127.0.0.1"));
    EXPECT_EQ(PortMapping::Create({Port(8080, "127.0.0.1")}).GetProbeHost(8080), std::string("127.0.0.1"));
    EXPECT_EQ(PortMapping::Create({Port(8080, "192.168.1.10")}).GetProbeHost(8080), std::string("192.168.1.10"));
    EXPECT_EQ(PortMapping::Create({Port(8080, "::")}).GetProbeHost(8080), std::string("::1"));
    EXPECT_EQ(PortMapping::Create({Port(8080, "::1")}).GetProbeHost(8080), std::string("::1"));
}

TEST(PortMapping, InspectPayloadResolvesDynamicPorts)
{
    const std::string inspect = "{\"Ports\":{\"8080/tcp\":[{\"HostIp\":\"127.0.0.1\",\"HostPort\":\"4514\"}],"
                                "\"5432/tcp\":[{\"HostIp\":\"127.0.0.1\",\"HostPort\":\"4515\"}]}}";

    PortMapping mapping = PortMapping::Create({Port(8080), Port(5432), Port(9090)});
    mapping.ResolveFromInspect(inspect);

    EXPECT_EQ(mapping.GetMappedPort(8080), 4514);
    EXPECT_EQ(mapping.GetMappedPort(5432), 4515);
    const std::vector<std::string> unresolved = mapping.UnresolvedPorts();
    ASSERT_EQ(unresolved.size(), 1u);
    EXPECT_EQ(unresolved[0], std::string("9090"));
}

TEST(PortMapping, InspectPayloadAcceptsNumericHostPorts)
{
    PortMapping mapping = PortMapping::Create({Port(8080)});
    mapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":4514}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4514);
}

TEST(PortMapping, InspectPayloadIgnoresInvalidHostPorts)
{
    PortMapping mapping = PortMapping::Create({Port(8080)});
    mapping.ResolveFromInspect(
        "{\"Ports\":{\"8080/"
        "tcp\":[{\"HostPort\":{}},{\"HostPort\":\"abc\"},{\"HostPort\":\"70000\"},{\"HostPort\":\"4515\"}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4515);
}

TEST(PortMapping, InspectPayloadRejectsLaxPortStrings)
{
    PortMapping mapping = PortMapping::Create({Port(8080)});
    mapping.ResolveFromInspect("{\"Ports\":{\"8080/"
                               "tcp\":[{\"HostPort\":\" 8080\"},{\"HostPort\":\"+8080\"},{\"HostPort\":\"4514.9\"},"
                               "{\"HostPort\":\"8080.0\"},{\"HostPort\":\"0\"},{\"HostPort\":\"65536\"},"
                               "{\"HostPort\":\"-1\"},{\"HostPort\":\"1e2\"},{\"HostPort\":\"4515\"}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4515);
}

TEST(PortMapping, InspectPayloadRejectsFractionalNumericHostPorts)
{
    PortMapping mapping = PortMapping::Create({Port(8080)});
    mapping.ResolveFromInspect(
        "{\"Ports\":{\"8080/tcp\":[{\"HostPort\":4514.9},{\"HostPort\":-1},{\"HostPort\":65536.0},"
        "{\"HostPort\":1e999},{\"HostPort\":4515}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4515);
}

TEST(PortMapping, InspectPayloadAcceptsLeadingZeros)
{
    PortMapping mapping = PortMapping::Create({Port(8080), Port(9090)});
    mapping.ResolveFromInspect("{\"Ports\":{\"08080/tcp\":[{\"HostPort\":\"04514\"}],"
                               "\"9090\":[{\"HostPort\":\" 9090\"},{\"HostPort\":\"9095\"}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4514);
    EXPECT_EQ(mapping.GetMappedPort(9090), 9095);
}

TEST(PortMapping, InspectPayloadRejectsLaxPortKeys)
{
    PortMapping mapping = PortMapping::Create({Port(8080), Port(8081), Port(8082), Port(8083), Port(8084), Port(8085)});
    mapping.ResolveFromInspect(
        "{\"Ports\":{\" 8080/tcp\":[{\"HostPort\":\"4514\"}],\"+8081/tcp\":[{\"HostPort\":\"4515\"}],"
        "\"8082.0/tcp\":[{\"HostPort\":\"4516\"}],\"0/tcp\":[{\"HostPort\":\"4517\"}],"
        "\"65536/tcp\":[{\"HostPort\":\"4518\"}],\"-1/tcp\":[{\"HostPort\":\"4519\"}]}}");

    for (const int port : {8080, 8081, 8082, 8083, 8084, 8085})
    {
        EXPECT_THROW(mapping.GetMappedPort(port), WslNetworkException) << "port " << port;
    }
}

TEST(PortMapping, UnknownPortsThrow)
{
    const PortMapping mapping = PortMapping::Create({Port(8080)});

    EXPECT_THROW(mapping.GetMappedPort(9090), WslNetworkException);
}

TEST(PortMapping, InspectPayloadHandlesProtocolSuffixVariants)
{
    PortMapping mapping = PortMapping::Create({Port(8080), Port(8081), Port(8082)});
    mapping.ResolveFromInspect("{\"Ports\":{\"8080/\":[{\"HostPort\":\"4514\"}],\"8081/UDP\":[{\"HostPort\":\"4515\"}],"
                               "\"8082/sctp\":[{\"HostPort\":\"4516\"}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4514);
    EXPECT_THROW(mapping.GetMappedPort(8081), WslNetworkException);
    EXPECT_THROW(mapping.GetMappedPort(8082), WslNetworkException);
}

TEST(PortMapping, InspectPayloadAcceptsBarePortKeys)
{
    PortMapping mapping = PortMapping::Create({Port(8080)});
    mapping.ResolveFromInspect("{\"Ports\":{\"8080\":[{\"HostPort\":\"4514\"}]}}");

    EXPECT_EQ(mapping.GetMappedPort(8080), 4514);
}

TEST(PortMapping, DuplicateMappingsKeepTheFirstBindAddress)
{
    const PortMapping mapping = PortMapping::Create({Port(8080, "127.0.0.1"), Port(8080, "0.0.0.0")});

    std::vector<sockaddr_storage> storage;
    const std::vector<WslcContainerPortMapping> mappings = mapping.ToNativeMappings(storage);
    ASSERT_EQ(mappings.size(), 1u);
    ASSERT_NE(mappings[0].windowsAddress, nullptr);
    EXPECT_EQ(FormatAddress(reinterpret_cast<const sockaddr*>(mappings[0].windowsAddress)), std::string("127.0.0.1"));
}

TEST(PortMapping, OpenHostPortsAreDetectedAndClosedPortsAreNot)
{
    LoopbackListener listener(AF_INET);
    {
        PortMapping mapping = PortMapping::Create({Port(8080)});
        mapping.ResolveFromInspect(InspectPayload(listener.Port()));
        EXPECT_TRUE(mapping.IsPortOpen(8080, std::stop_token{}));
    }

    // Hold an IPv6 listener open and probe its port over IPv4 loopback: the port is never
    // freed for reuse, so the "closed" assertion cannot race another process.
    LoopbackListener closedListener(AF_INET6);
    PortMapping closedMapping = PortMapping::Create({Port(8080)});
    closedMapping.ResolveFromInspect(InspectPayload(closedListener.Port()));
    EXPECT_FALSE(closedMapping.IsPortOpen(8080, std::stop_token{}));
}
