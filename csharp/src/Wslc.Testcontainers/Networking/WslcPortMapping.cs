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
/// inspect payload; a fixed host port is passed through and is known before start. Mappings are
/// TCP-only: the runtime returns <c>E_NOTIMPL</c> for UDP, so non-TCP inspect entries are ignored.
/// TCP probes honor the configured bind address (any-address bindings probe loopback).
/// </summary>
internal sealed class WslcPortMapping
{
    private const string InspectPortsProperty = "Ports";

    private readonly Dictionary<int, Entry> _entries;

    private sealed class Entry
    {
        public string? BindAddress { get; }
        // Probe address resolved once at Create so readiness polls (every ~250 ms) do not
        // re-parse the bind address per probe. Matches the C++ PortMapping::ProbeHost cache.
        public IPAddress ProbeAddress { get; }
        // Fixed host port reserved by the builder, or 0 for a runtime-assigned dynamic port.
        public int HostPort { get; }
        public int MappedPort { get; set; }

        public Entry(string? bindAddress, int hostPort)
        {
            BindAddress = bindAddress;
            ProbeAddress = ResolveProbeAddress(bindAddress);
            HostPort = hostPort;
            // A fixed port is known before start, so it resolves without polling the inspect
            // payload; dynamic ports stay 0 until ResolveFromInspect assigns one.
            MappedPort = hostPort;
        }

        private static IPAddress ResolveProbeAddress(string? bindAddress)
        {
            if (bindAddress is null || !IPAddress.TryParse(bindAddress, out var address))
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
                entries[mapping.ContainerPort] = new Entry(mapping.BindAddress, mapping.HostPort);
            }
        }

        return new WslcPortMapping(entries);
    }

    public IReadOnlyList<ContainerPortMapping> ToContainerPortMappings()
    {
        var mappings = new List<ContainerPortMapping>(_entries.Count);
        foreach (var pair in _entries)
        {
            var mapping = new ContainerPortMapping((ushort)pair.Value.HostPort, (ushort)pair.Key, Microsoft.WSL.Containers.PortProtocol.TCP);
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
        // The runtime may return partial JSON while ports are being assigned; like the C++ port,
        // treat unparsable payloads as "not yet resolved" so the poll loop retries instead of failing startup.
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(inspectJson);
        }
        catch (JsonException)
        {
            return;
        }

        using (document)
        {
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
                    if (item.ValueKind == JsonValueKind.Object &&
                        item.TryGetProperty("HostPort", out var hostPortElement) &&
                        TryReadMappedPort(hostPortElement, out var hostPort))
                    {
                        entry.MappedPort = hostPort;
                        break;
                    }
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
        var endpoint = GetConnectEndpoint(containerPort);
        using var client = new TcpClient();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(2));

        try
        {
            await client.ConnectAsync(endpoint.Address, endpoint.Port, timeout.Token).ConfigureAwait(false);
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

    /// <summary>
    /// Gets the Windows endpoint a readiness probe should connect to for a mapped TCP port:
    /// the runtime-assigned host port and the address the mapping is actually bound to.
    /// The default/wildcard binding resolves to loopback: IPv4 <c>127.0.0.1</c> for
    /// <c>0.0.0.0</c> and IPv6 <c>::1</c> for <c>::</c>.
    /// </summary>
    public IPEndPoint GetConnectEndpoint(int containerPort) =>
        new(GetProbeAddress(containerPort), GetMappedPort(containerPort));

    /// <summary>
    /// Returns the cached probe address for a mapped port. Wildcard bindings resolve to their
    /// loopback (<c>0.0.0.0</c> to IPv4, <c>::</c> to IPv6); an unbound or unparsable entry falls
    /// back to the SDK default (IPv4 loopback).
    /// </summary>
    private IPAddress GetProbeAddress(int containerPort) =>
        _entries.TryGetValue(containerPort, out var entry) ? entry.ProbeAddress : IPAddress.Loopback;
}
