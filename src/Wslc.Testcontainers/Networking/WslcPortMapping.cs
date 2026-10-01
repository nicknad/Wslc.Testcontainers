using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using Microsoft.WSL.Containers;

namespace Wslc.Testcontainers.Networking;

/// <summary>
/// Maps Linux container ports to Windows loopback ports using the official
/// <see cref="ContainerPortMapping"/> mechanism. A Windows port of 0 asks the WSL runtime
/// to assign a free dynamic port, which is then discovered from the container inspect payload.
/// </summary>
internal sealed class WslcPortMapping : IWslNetwork
{
    private const string InspectPortsProperty = "Ports";

    private readonly Dictionary<int, int> _ports;

    private WslcPortMapping(Dictionary<int, int> ports) => _ports = ports;

    public string Host => IPAddress.Loopback.ToString();

    public IReadOnlyCollection<int> Ports => _ports.Keys;

    public int UnresolvedCount
    {
        get
        {
            var count = 0;
            foreach (var pair in _ports)
            {
                if (pair.Value == 0)
                {
                    count++;
                }
            }

            return count;
        }
    }

    public IReadOnlyList<int> UnresolvedPorts
    {
        get
        {
            var count = UnresolvedCount;
            if (count == 0)
            {
                return Array.Empty<int>();
            }

            var result = new List<int>(count);
            foreach (var pair in _ports)
            {
                if (pair.Value == 0)
                {
                    result.Add(pair.Key);
                }
            }

            return result;
        }
    }

    public static WslcPortMapping Create(IReadOnlyCollection<int> containerPorts)
    {
        var ports = new Dictionary<int, int>(containerPorts.Count);
        foreach (var port in containerPorts)
        {
            ports[port] = 0;
        }

        return new WslcPortMapping(ports);
    }

    public IReadOnlyList<ContainerPortMapping> ToContainerPortMappings()
    {
        var mappings = new List<ContainerPortMapping>(_ports.Count);
        foreach (var containerPort in _ports.Keys)
        {
            mappings.Add(new ContainerPortMapping(0, (ushort)containerPort, PortProtocol.TCP));
        }

        return mappings;
    }

    public int GetMappedPort(int containerPort)
    {
        if (!_ports.TryGetValue(containerPort, out var mappedPort))
        {
            throw new WslNetworkException(
                $"Port {containerPort} is not mapped. Declare it with WithPort({containerPort}) before starting the container.");
        }

        if (mappedPort == 0)
        {
            throw new WslNetworkException(
                $"Port {containerPort} has no host mapping yet. The WSL runtime assigns the port when the container starts.");
        }

        return mappedPort;
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
            var separator = property.Name.IndexOf('/');
            if (separator <= 0 || !int.TryParse(property.Name[..separator], out var containerPort))
            {
                continue;
            }

            if (!_ports.ContainsKey(containerPort) || property.Value.ValueKind != JsonValueKind.Array)
            {
                continue;
            }

            foreach (var entry in property.Value.EnumerateArray())
            {
                if (entry.TryGetProperty("HostPort", out var hostPortElement) &&
                    ushort.TryParse(hostPortElement.GetString(), out var hostPort) &&
                    hostPort != 0)
                {
                    _ports[containerPort] = hostPort;
                    break;
                }
            }
        }
    }

    public async Task<bool> IsPortOpenAsync(int containerPort, CancellationToken cancellationToken = default)
    {
        var mappedPort = GetMappedPort(containerPort);
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

    public ValueTask DisposeAsync()
    {
        _ports.Clear();
        return ValueTask.CompletedTask;
    }
}
