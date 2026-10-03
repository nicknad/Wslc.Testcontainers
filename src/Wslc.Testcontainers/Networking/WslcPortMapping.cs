using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Provisioning;

namespace Wslc.Testcontainers.Networking;

/// <summary>
/// Maps Linux container ports to Windows ports (loopback by default, overridable per port)
/// using the official <see cref="ContainerPortMapping"/> mechanism. A Windows port of 0 asks
/// the WSL runtime to assign a free dynamic port, which is then discovered from the container
/// inspect payload. Mappings are TCP-only: the runtime returns <c>E_NOTIMPL</c> for UDP, so
/// non-TCP inspect entries are ignored. TCP probes honor the configured bind address
/// (any-address bindings probe loopback).
/// </summary>
internal sealed class WslcPortMapping
{
    private const string InspectPortsProperty = "Ports";

    private readonly Dictionary<int, Entry> _entries;

    private sealed class Entry
    {
        public string? BindAddress { get; }
        public int MappedPort { get; set; }

        public Entry(string? bindAddress) => BindAddress = bindAddress;
    }

    private WslcPortMapping(Dictionary<int, Entry> entries) => _entries = entries;

    public int UnresolvedCount
    {
        get
        {
            var count = 0;
            foreach (var entry in _entries.Values)
            {
                if (entry.MappedPort == 0)
                {
                    count++;
                }
            }

            return count;
        }
    }

    public IReadOnlyList<string> UnresolvedPorts
    {
        get
        {
            var result = new List<string>(_entries.Count);
            foreach (var pair in _entries)
            {
                if (pair.Value.MappedPort == 0)
                {
                    result.Add(pair.Key.ToString(CultureInfo.InvariantCulture));
                }
            }

            return result;
        }
    }

    public static WslcPortMapping Create(IEnumerable<WslPortMapping> mappings)
    {
        var entries = new Dictionary<int, Entry>();
        foreach (var mapping in mappings)
        {
            if (!entries.ContainsKey(mapping.ContainerPort))
            {
                entries[mapping.ContainerPort] = new Entry(mapping.BindAddress);
            }
        }

        return new WslcPortMapping(entries);
    }

    public IReadOnlyList<ContainerPortMapping> ToContainerPortMappings()
    {
        var mappings = new List<ContainerPortMapping>(_entries.Count);
        foreach (var pair in _entries)
        {
            var mapping = new ContainerPortMapping(0, (ushort)pair.Key, Microsoft.WSL.Containers.PortProtocol.TCP);
            if (pair.Value.BindAddress is { } bindAddress)
            {
                mapping.WindowsAddress = new Windows.Networking.HostName(bindAddress);
            }

            mappings.Add(mapping);
        }

        return mappings;
    }

    public int GetMappedPort(int containerPort)
    {
        if (!_entries.TryGetValue(containerPort, out var entry))
        {
            throw new WslNetworkException(
                $"Port {containerPort} is not mapped. Declare it with WithPort({containerPort}) before starting the container.");
        }

        if (entry.MappedPort == 0)
        {
            throw new WslNetworkException(
                $"Port {containerPort} has no host mapping yet. The WSL runtime assigns the port when the container starts.");
        }

        return entry.MappedPort;
    }

    /// <summary>Reads the dynamically assigned host ports from <c>Container.Inspect()</c>.</summary>
    internal void ResolveFromInspect(string inspectJson)
    {
        using var document = JsonDocument.Parse(inspectJson);
        if (!document.RootElement.TryGetProperty(InspectPortsProperty, out var ports) || ports.ValueKind != JsonValueKind.Object)
        {
            return;
        }

        foreach (var property in ports.EnumerateObject())
        {
            if (!TryParseInspectPort(property.Name, out var containerPort))
            {
                continue;
            }

            if (!_entries.TryGetValue(containerPort, out var entry) || property.Value.ValueKind != JsonValueKind.Array)
            {
                continue;
            }

            foreach (var item in property.Value.EnumerateArray())
            {
                if (item.TryGetProperty("HostPort", out var hostPortElement) &&
                    TryReadMappedPort(hostPortElement, out var hostPort))
                {
                    entry.MappedPort = hostPort;
                    break;
                }
            }
        }
    }

    /// <summary>
    /// Parses an inspect port key. TCP keys are <c>port</c>, <c>port/</c> and
    /// <c>port/tcp</c> (suffix case-insensitive); UDP/other protocols are skipped so
    /// they can never resolve a TCP declaration.
    /// </summary>
    private static bool TryParseInspectPort(string key, out int containerPort)
    {
        containerPort = 0;

        var separator = key.IndexOf('/');
        var portPart = separator < 0 ? key : key.Substring(0, separator);
        if (portPart.Length == 0 ||
            !int.TryParse(portPart, NumberStyles.None, CultureInfo.InvariantCulture, out containerPort))
        {
            return false;
        }

        if (separator < 0)
        {
            return true;
        }

        var suffix = key.Substring(separator + 1);
        return suffix.Length == 0 || suffix.Equals("tcp", StringComparison.OrdinalIgnoreCase);
    }

    private static bool TryReadMappedPort(JsonElement element, out int port)
    {
        port = 0;
        var parsed = element.ValueKind switch
        {
            JsonValueKind.String => int.TryParse(element.GetString(), NumberStyles.None, CultureInfo.InvariantCulture, out port),
            JsonValueKind.Number => element.TryGetInt32(out port),
            _ => false,
        };

        return parsed && port is > 0 and <= 65535;
    }

    public async Task<bool> IsPortOpenAsync(int containerPort, CancellationToken cancellationToken = default)
    {
        var mappedPort = GetMappedPort(containerPort);
        var probeAddress = ResolveProbeAddress(containerPort);
        using var client = new TcpClient();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(2));

        try
        {
            await client.ConnectAsync(probeAddress, mappedPort, timeout.Token).ConfigureAwait(false);
            return true;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch
        {
            return false;
        }
    }

    /// <summary>Gets the host a readiness probe should connect to for a mapped TCP port.</summary>
    public string GetProbeHost(int containerPort) => ResolveProbeAddress(containerPort).ToString();

    /// <summary>
    /// Connects to the address the mapping is actually bound to. Wildcard bindings
    /// (<c>0.0.0.0</c>/<c>::</c>) accept loopback; an unbound or unparsable entry falls
    /// back to the SDK default (IPv4 loopback).
    /// </summary>
    private IPAddress ResolveProbeAddress(int containerPort)
    {
        if (!_entries.TryGetValue(containerPort, out var entry) ||
            entry.BindAddress is not { } bindAddress ||
            !IPAddress.TryParse(bindAddress, out var address))
        {
            return IPAddress.Loopback;
        }

        if (address.Equals(IPAddress.Any))
        {
            return IPAddress.Loopback;
        }

        if (address.Equals(IPAddress.IPv6Any))
        {
            return IPAddress.IPv6Loopback;
        }

        return address;
    }
}
