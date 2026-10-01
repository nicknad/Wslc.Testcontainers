using System.Net;
using System.Net.Sockets;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Networking;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslcPortMappingTests
{
    [Fact]
    public void Mappings_are_dynamic_until_resolved()
    {
        var mapping = WslcPortMapping.Create(new[] { 5432, 8080, 5432 });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(5432));

        var containerMappings = mapping.ToContainerPortMappings();
        Assert.Equal(2, containerMappings.Count);
        Assert.All(containerMappings, item => Assert.Equal((ushort)0, item.WindowsPort));
        Assert.All(containerMappings, item => Assert.Equal(PortProtocol.TCP, item.Protocol));
        Assert.Equal(new[] { 5432, 8080 }, containerMappings.Select(item => (int)item.ContainerPort).OrderBy(port => port));
    }

    [Fact]
    public void Inspect_payload_resolves_dynamic_ports()
    {
        const string inspectJson =
            """{"Ports":{"8080/tcp":[{"HostIp":"127.0.0.1","HostPort":"4514"}],"5432/tcp":[{"HostIp":"127.0.0.1","HostPort":"4515"}]}}""";

        var mapping = WslcPortMapping.Create(new[] { 8080, 5432, 9090 });
        mapping.ResolveFromInspect(inspectJson);

        Assert.Equal(4514, mapping.GetMappedPort(8080));
        Assert.Equal(4515, mapping.GetMappedPort(5432));
        Assert.Equal(new[] { 9090 }, mapping.UnresolvedPorts);
    }

    [Fact]
    public void Inspect_payload_accepts_numeric_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { 8080 });
        mapping.ResolveFromInspect("""{"Ports":{"8080/tcp":[{"HostPort":4514}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_ignores_invalid_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { 8080 });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostPort":{}},{"HostPort":"abc"},{"HostPort":"70000"},{"HostPort":"4515"}]}}""");

        Assert.Equal(4515, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Unknown_ports_throw()
    {
        var mapping = WslcPortMapping.Create(new[] { 8080 });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(9090));
    }

    [Fact]
    public async Task Open_host_ports_are_detected_and_closed_ports_are_not()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;

        try
        {
            var mapping = WslcPortMapping.Create(new[] { 8080 });
            mapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + port + "\"}]}}");

            Assert.True(await mapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
        }
        finally
        {
            listener.Stop();
        }

        var closedMapping = WslcPortMapping.Create(new[] { 8080 });
        closedMapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + port + "\"}]}}");

        Assert.False(await closedMapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
    }
}
