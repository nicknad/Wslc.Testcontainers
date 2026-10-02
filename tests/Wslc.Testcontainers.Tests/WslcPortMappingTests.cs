using System.Net;
using System.Net.Sockets;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Networking;
using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslcPortMappingTests
{
    private static WslPortMapping Tcp(int port, string? bindAddress = null) => new(port, PortProtocol.TCP, bindAddress);

    [Fact]
    public void Mappings_are_dynamic_until_resolved()
    {
        var mapping = WslcPortMapping.Create(new[] { Tcp(5432), Tcp(8080), Tcp(5432) });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(5432));

        var containerMappings = mapping.ToContainerPortMappings();
        Assert.Equal(2, containerMappings.Count);
        Assert.All(containerMappings, item => Assert.Equal((ushort)0, item.WindowsPort));
        Assert.All(containerMappings, item => Assert.Equal(PortProtocol.TCP, item.Protocol));
        Assert.Equal(new[] { 5432, 8080 }, containerMappings.Select(item => (int)item.ContainerPort).OrderBy(port => port));
    }

    [Fact]
    public void Bind_address_is_forwarded_to_the_container_mapping()
    {
        var mapping = WslcPortMapping.Create(new[] { Tcp(8080, "127.0.0.1"), Tcp(9090) });

        var mappings = mapping.ToContainerPortMappings();
        var bound = Assert.Single(mappings, item => item.ContainerPort == 8080);
        var unbound = Assert.Single(mappings, item => item.ContainerPort == 9090);

        Assert.NotNull(bound.WindowsAddress);
        Assert.Equal("127.0.0.1", bound.WindowsAddress.RawName);
        Assert.Null(unbound.WindowsAddress);
    }

    [Fact]
    public void Udp_missing_port_hint_suggests_WithUdpPort()
    {
        var mapping = WslcPortMapping.Create(Array.Empty<WslPortMapping>());

        var exception = Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(53, PortProtocol.UDP));

        Assert.Contains("WithUdpPort(53)", exception.Message);
    }

    [Fact]
    public void Same_port_with_tcp_and_udp_maps_independently()
    {
        var mapping = WslcPortMapping.Create(new[] { new WslPortMapping(8080, PortProtocol.TCP, null), new WslPortMapping(8080, PortProtocol.UDP, null) });

        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostIp":"127.0.0.1","HostPort":"4514"}],"8080/udp":[{"HostIp":"127.0.0.1","HostPort":"4515"}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080, PortProtocol.TCP));
        Assert.Equal(4515, mapping.GetMappedPort(8080, PortProtocol.UDP));
        Assert.Equal(4514, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_resolves_dynamic_ports()
    {
        const string inspectJson =
            """{"Ports":{"8080/tcp":[{"HostIp":"127.0.0.1","HostPort":"4514"}],"5432/tcp":[{"HostIp":"127.0.0.1","HostPort":"4515"}]}}""";

        var mapping = WslcPortMapping.Create(new[] { Tcp(8080), Tcp(5432), Tcp(9090) });
        mapping.ResolveFromInspect(inspectJson);

        Assert.Equal(4514, mapping.GetMappedPort(8080));
        Assert.Equal(4515, mapping.GetMappedPort(5432));
        Assert.Equal(new[] { "9090" }, mapping.UnresolvedPorts);
    }

    [Fact]
    public void Inspect_payload_accepts_numeric_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { Tcp(8080) });
        mapping.ResolveFromInspect("""{"Ports":{"8080/tcp":[{"HostPort":4514}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_ignores_invalid_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { Tcp(8080) });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostPort":{}},{"HostPort":"abc"},{"HostPort":"70000"},{"HostPort":"4515"}]}}""");

        Assert.Equal(4515, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Unknown_ports_throw()
    {
        var mapping = WslcPortMapping.Create(new[] { Tcp(8080) });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(9090));
        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(8080, PortProtocol.UDP));
    }

    [Fact]
    public async Task Open_host_ports_are_detected_and_closed_ports_are_not()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;

        try
        {
            var mapping = WslcPortMapping.Create(new[] { Tcp(8080) });
            mapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + port + "\"}]}}");

            Assert.True(await mapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
        }
        finally
        {
            listener.Stop();
        }

        var closedMapping = WslcPortMapping.Create(new[] { Tcp(8080) });
        closedMapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + port + "\"}]}}");

        Assert.False(await closedMapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
    }
}
