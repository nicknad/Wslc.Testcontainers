#include "internal/port_mapping.hpp"

#include "internal/tcp_http.hpp"
#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace wslc::internal
{

namespace
{

bool TryParsePortDigits(std::string_view text, int& port)
{
    port = 0;
    if (text.empty())
    {
        return false;
    }

    std::uint32_t parsed = 0;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            return false;
        }

        parsed = (parsed * 10) + static_cast<std::uint32_t>(character - '0');
        if (parsed > 65535)
        {
            return false;
        }
    }

    port = static_cast<int>(parsed);
    return port > 0;
}

bool TryParseInspectPort(std::string_view key, int& ContainerPort)
{
    ContainerPort = 0;
    const std::size_t separator = key.find('/');
    const std::string_view port_part = separator == std::string_view::npos ? key : key.substr(0, separator);
    if (!TryParsePortDigits(port_part, ContainerPort))
    {
        return false;
    }

    if (separator == std::string_view::npos)
    {
        return true;
    }

    const std::string_view suffix = key.substr(separator + 1);
    return suffix.empty() || EqualsIgnoreCase(suffix, "tcp");
}

bool TryReadMappedPort(const nlohmann::json& element, int& port)
{
    port = 0;
    if (element.is_string())
    {
        return TryParsePortDigits(element.get_ref<const std::string&>(), port);
    }

    if (element.is_number())
    {
        const double value = element.get<double>();
        if (!std::isfinite(value) || value < 1.0 || value > 65535.0 || value != std::trunc(value))
        {
            return false;
        }

        port = static_cast<int>(value);
        return true;
    }

    return false;
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

PortMapping::PortMapping(const std::vector<WslPortMappingRecord>& mappings)
{
    for (const auto& mapping : mappings)
    {
        if (!m_entries.contains(mapping.ContainerPort))
        {
            Entry entry;
            entry.BindAddress = mapping.BindAddress;
            entry.ProbeHost = ResolveProbeHost(mapping.BindAddress);
            m_entries[mapping.ContainerPort] = std::move(entry);
        }
    }
}

int PortMapping::UnresolvedCount() const
{
    std::lock_guard lock(m_gate);
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
    std::lock_guard lock(m_gate);
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
    std::lock_guard lock(m_gate);
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

int PortMapping::GetMappedPort(int containerPort) const
{
    std::lock_guard lock(m_gate);
    return GetMappedPortLocked(containerPort);
}

int PortMapping::GetMappedPortLocked(int containerPort) const
{
    const auto it = m_entries.find(containerPort);
    if (it == m_entries.end())
    {
        throw WslNetworkException("Port " + std::to_string(containerPort) +
                                  " is not mapped. Declare it with WithPort(" + std::to_string(containerPort) +
                                  ") before starting the container.");
    }

    if (it->second.MappedPort == 0)
    {
        throw WslNetworkException(
            "Port " + std::to_string(containerPort) +
            " has no host mapping yet. The WSL runtime assigns the port when the container starts.");
    }

    return it->second.MappedPort;
}

void PortMapping::ResolveFromInspect(std::string_view inspect_json)
{
    // The runtime may return partial JSON while ports are being assigned; treat unparsable
    // payloads as "not yet resolved" so the poll loop retries instead of failing startup.
    // Note: a number that overflows double (e.g. 1e999) makes nlohmann/json reject the whole
    // document, so even valid entries stay unresolved until the next poll. The runtime only
    // emits small integers and strings, where per-value skipping still applies.
    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(inspect_json.begin(), inspect_json.end());
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }

    if (!document.is_object())
    {
        return;
    }

    const auto ports = document.find("Ports");
    if (ports == document.end() || !ports->is_object())
    {
        return;
    }

    std::lock_guard lock(m_gate);
    for (const auto& [key, value] : ports->items())
    {
        int ContainerPort = 0;
        if (!TryParseInspectPort(key, ContainerPort))
        {
            continue;
        }

        const auto it = m_entries.find(ContainerPort);
        if (it == m_entries.end() || !value.is_array())
        {
            continue;
        }

        for (const auto& item : value)
        {
            if (!item.is_object())
            {
                continue;
            }

            const auto hostPort = item.find("HostPort");
            int mapped = 0;
            if (hostPort != item.end() && TryReadMappedPort(*hostPort, mapped))
            {
                it->second.MappedPort = mapped;
                break;
            }
        }
    }
}

bool PortMapping::IsPortOpen(int containerPort, std::stop_token token) const
{
    const WslEndpoint endpoint = GetConnectEndpoint(containerPort);
    return TcpProbe(endpoint.Host, endpoint.Port, std::chrono::seconds(2), token);
}

WslEndpoint PortMapping::GetConnectEndpoint(int containerPort) const
{
    std::lock_guard lock(m_gate);
    const auto it = m_entries.find(containerPort);
    const std::string host = it != m_entries.end() ? it->second.ProbeHost : "127.0.0.1";

    return WslEndpoint{host, GetMappedPortLocked(containerPort)};
}

std::string PortMapping::ResolveProbeHost(const std::optional<std::string>& bindAddress)
{
    if (!bindAddress)
    {
        return "127.0.0.1";
    }

    const auto normalized = NormalizeIpAddress(*bindAddress);
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
