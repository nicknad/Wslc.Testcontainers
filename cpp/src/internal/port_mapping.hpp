#pragma once

#include "internal/configuration.hpp"
#include "wslc/endpoint.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wslcsdk.h>

#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace wslc::internal
{

/// <summary>
/// Maps Linux container ports to Windows ports (loopback by default, overridable per port)
/// using the official container port mapping mechanism. A Windows port of 0 asks the runtime
/// to assign a free dynamic port, discovered from the container inspect payload. Mappings are
/// TCP-only: the runtime returns E_NOTIMPL for UDP, so non-TCP inspect entries are ignored.
/// </summary>
class PortMapping
{
public:
    static PortMapping Create(const std::vector<WslPortMappingRecord>& mappings);

    int UnresolvedCount() const;
    std::vector<std::string> UnresolvedPorts() const;

    /// <summary>Builds native mappings; storage holds the sockaddr values referenced by them.</summary>
    std::vector<WslcContainerPortMapping> ToNativeMappings(std::vector<sockaddr_storage>& storage) const;

    int GetMappedPort(int containerPort) const;

    /// <summary>Reads the dynamically assigned Host ports from the container inspect payload.</summary>
    void ResolveFromInspect(std::string_view inspect_json);

    bool IsPortOpen(int containerPort, std::stop_token token) const;

    /// <summary>Gets the Windows endpoint a readiness probe should connect to for a mapped TCP port:
    /// the runtime-assigned Host port and the address the mapping is actually bound to. The
    /// default/wildcard binding resolves to loopback: IPv4 127.0.0.1 for 0.0.0.0, IPv6 ::1 for ::.</summary>
    WslEndpoint GetConnectEndpoint(int containerPort) const;

private:
    struct Entry
    {
        std::optional<std::string> BindAddress;
        int MappedPort = 0;
    };

    std::map<int, Entry> m_entries;
};

} // namespace wslc::internal
