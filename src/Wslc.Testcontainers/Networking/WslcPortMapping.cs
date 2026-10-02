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
/// inspect payload. Entries are keyed by (container port, protocol); the single-port API is TCP.
/// </summary>
internal sealed class WslcPortMapping
{
    private const string InspectPortsProperty = "Ports";

    private readonly Dictionary<(int Port, PortProtocol Protocol), Entry> _entries;

    private sealed class Entry
    {
        public string? BindAddress { get; }
        public int MappedPort { get; set; }

        public Entry(string? bindAddress) => BindAddress = bindAddress;
    }

    private WslcPortMapping(Dictionary<(int Port, PortProtocol Protocol), Entry> entries) => _entries = entries;

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
                    result.Add(WslPortMapping.Format(pair.Key.Port, pair.Key.Protocol));
                }
            }

            return result;
        }
    }

    public static WslcPortMapping Create(IEnumerable<WslPortMapping> mappings)
    {
        var entries = new Dictionary<(int Port, PortProtocol Protocol), Entry>();
        foreach (var mapping in mappings)
        {
            var key = (mapping.ContainerPort, mapping.Protocol);
            if (!entries.ContainsKey(key))
            {
                entries[key] = new Entry(mapping.BindAddress);
            }
        }

        return new WslcPortMapping(entries);
    }

    public IReadOnlyList<ContainerPortMapping> ToContainerPortMappings()
    {
        var mappings = new List<ContainerPortMapping>(_entries.Count);
        foreach (var pair in _entries)
        {
            var mapping = new ContainerPortMapping(0, (ushort)pair.Key.Port, pair.Key.Protocol);
            if (pair.Value.BindAddress is { } bindAddress)
            {
                mapping.WindowsAddress = new Windows.Networking.HostName(bindAddress);
            }

            mappings.Add(mapping);
        }

        return mappings;
    }

    public int GetMappedPort(int containerPort) => GetMappedPort(containerPort, PortProtocol.TCP);

    public int GetMappedPort(int containerPort, PortProtocol protocol)
    {
        if (!_entries.TryGetValue((containerPort, protocol), out var entry))
        {
            throw new WslNetworkException(
                $"Port {WslPortMapping.Format(containerPort, protocol)} is not mapped. Declare it with {DescribeDeclaration(containerPort, protocol)} before starting the container.");
        }

        if (entry.MappedPort == 0)
        {
            throw new WslNetworkException(
                $"Port {WslPortMapping.Format(containerPort, protocol)} has no host mapping yet. The WSL runtime assigns the port when the container starts.");
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
            if (!TryParseInspectKey(property.Name, out var containerPort, out var protocol))
            {
                continue;
            }

            if (!_entries.TryGetValue((containerPort, protocol), out var entry) || property.Value.ValueKind != JsonValueKind.Array)
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

    private static bool TryParseInspectKey(string key, out int containerPort, out PortProtocol protocol)
    {
        containerPort = 0;
        protocol = PortProtocol.TCP;

        var separator = key.IndexOf('/');
        if (separator <= 0 ||
            !int.TryParse(key.AsSpan(0, separator), NumberStyles.None, CultureInfo.InvariantCulture, out containerPort))
        {
            return false;
        }

        var suffix = key.Substring(separator + 1);
        if (suffix.Length == 0)
        {
            return true;
        }

        if (suffix.Equals("tcp", StringComparison.OrdinalIgnoreCase))
        {
            protocol = PortProtocol.TCP;
            return true;
        }

        if (suffix.Equals("udp", StringComparison.OrdinalIgnoreCase))
        {
            protocol = PortProtocol.UDP;
            return true;
        }

        return false;
    }

    private static string DescribeDeclaration(int containerPort, PortProtocol protocol) =>
        protocol == PortProtocol.UDP
            ? $"WithUdpPort({containerPort})"
            : $"WithPort({containerPort})";

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
        var mappedPort = GetMappedPort(containerPort, PortProtocol.TCP);
        using var client = new TcpClient();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(2));

        try
        {
            await client.ConnectAsync(IPAddress.Loopback, mappedPort, timeout.Token).ConfigureAwait(false);
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
}
