#include "internal/port_mapping.hpp"

#include "internal/json.hpp"
#include "internal/tcp_http.hpp"
#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <cstdlib>

namespace wslc::internal
{

namespace
{

bool TryParseInspectPort(std::string key, int& ContainerPort)
{
    ContainerPort = 0;
    const std::size_t separator = key.find('/');
    const std::string port_part = separator == std::string::npos ? key : key.substr(0, separator);
    if (port_part.empty())
    {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(port_part.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 1 || parsed > 65535)
    {
        return false;
    }

    ContainerPort = static_cast<int>(parsed);
    if (separator == std::string::npos)
    {
        return true;
    }

    const std::string suffix = key.substr(separator + 1);
    return suffix.empty() || EqualsIgnoreCase(suffix, "tcp");
}

bool TryReadMappedPort(const json::Value& element, int& port)
{
    port = 0;
    if (element.IsString())
    {
        char* end = nullptr;
        const long parsed = std::strtol(element.String.c_str(), &end, 10);
        if (end == nullptr || *end != '\0')
        {
            return false;
        }

        port = static_cast<int>(parsed);
    }
    else if (element.IsNumber())
    {
        port = static_cast<int>(element.Number);
    }
    else
    {
        return false;
    }

    return port > 0 && port <= 65535;
}

void FillSockaddr(const std::string& address, sockaddr_storage& storage)
{
    EnsureWinsock();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* result = nullptr;
    if (getaddrinfo(address.c_str(), "0", &hints, &result) != 0 || result == nullptr)
    {
        std::memset(&storage, 0, sizeof(storage));
        return;
    }

    std::memcpy(&storage, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
}

} // namespace

PortMapping PortMapping::Create(const std::vector<WslPortMappingRecord>& mappings)
{
    PortMapping result;
    for (const auto& mapping : mappings)
    {
        if (!result.m_entries.contains(mapping.ContainerPort))
        {
            result.m_entries[mapping.ContainerPort] = Entry{mapping.BindAddress, 0};
        }
    }

    return result;
}

int PortMapping::UnresolvedCount() const
{
    int count = 0;
    for (const auto& pair : m_entries)
    {
        if (pair.second.MappedPort == 0)
        {
            count++;
        }
    }

    return count;
}

std::vector<std::string> PortMapping::UnresolvedPorts() const
{
    std::vector<std::string> result;
    for (const auto& pair : m_entries)
    {
        if (pair.second.MappedPort == 0)
        {
            result.push_back(std::to_string(pair.first));
        }
    }

    return result;
}

std::vector<WslcContainerPortMapping> PortMapping::ToNativeMappings(std::vector<sockaddr_storage>& storage) const
{
    std::vector<WslcContainerPortMapping> mappings;
    mappings.reserve(m_entries.size());
    storage.reserve(storage.size() + m_entries.size());
    for (const auto& pair : m_entries)
    {
        WslcContainerPortMapping mapping{};
        mapping.windowsPort = 0;
        mapping.containerPort = static_cast<std::uint16_t>(pair.first);
        mapping.protocol = WSLC_PORT_PROTOCOL_TCP;
        mapping.windowsAddress = nullptr;
        if (pair.second.BindAddress)
        {
            storage.emplace_back();
            FillSockaddr(*pair.second.BindAddress, storage.back());
            mapping.windowsAddress = &storage.back();
        }

        mappings.push_back(mapping);
    }

    return mappings;
}

int PortMapping::GetMappedPort(int ContainerPort) const
{
    const auto it = m_entries.find(ContainerPort);
    if (it == m_entries.end())
    {
        throw WslNetworkException("Port " + std::to_string(ContainerPort) +
                                  " is not mapped. Declare it with WithPort(" + std::to_string(ContainerPort) +
                                  ") before starting the container.");
    }

    if (it->second.MappedPort == 0)
    {
        throw WslNetworkException(
            "Port " + std::to_string(ContainerPort) +
            " has no host mapping yet. The WSL runtime assigns the port when the container starts.");
    }

    return it->second.MappedPort;
}

void PortMapping::ResolveFromInspect(std::string_view inspect_json)
{
    const auto document = json::Parse(inspect_json);
    if (!document)
    {
        return;
    }

    const json::Value* ports = document->Find("Ports");
    if (ports == nullptr || !ports->IsObject())
    {
        return;
    }

    for (const auto& property : ports->Object)
    {
        int ContainerPort = 0;
        if (!TryParseInspectPort(property.first, ContainerPort))
        {
            continue;
        }

        const auto it = m_entries.find(ContainerPort);
        if (it == m_entries.end() || !property.second.IsArray())
        {
            continue;
        }

        for (const auto& item : property.second.Array)
        {
            if (!item.IsObject())
            {
                continue;
            }

            const json::Value* host_port = item.Find("HostPort");
            int mapped = 0;
            if (host_port != nullptr && TryReadMappedPort(*host_port, mapped))
            {
                it->second.MappedPort = mapped;
                break;
            }
        }
    }
}

bool PortMapping::IsPortOpen(int ContainerPort, std::stop_token token) const
{
    const int MappedPort = GetMappedPort(ContainerPort);
    const std::string probe_address = GetProbeHost(ContainerPort);
    return TcpProbe(probe_address, MappedPort, std::chrono::seconds(2), token);
}

std::string PortMapping::GetProbeHost(int ContainerPort) const
{
    const auto it = m_entries.find(ContainerPort);
    if (it == m_entries.end() || !it->second.BindAddress)
    {
        return "127.0.0.1";
    }

    const auto normalized = NormalizeIpAddress(*it->second.BindAddress);
    if (!normalized)
    {
        return "127.0.0.1";
    }

    if (*normalized == "0.0.0.0")
    {
        return "127.0.0.1";
    }

    if (*normalized == "::")
    {
        return "::1";
    }

    return *normalized;
}

} // namespace wslc::internal
