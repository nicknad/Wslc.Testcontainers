using System.Net;
using System.Net.Sockets;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Networking;
using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslcPortMappingTests
{
    private static WslPortMapping Port(int port, string? bindAddress = null) => new(port, bindAddress);

    [Fact]
    public void Mappings_are_dynamic_until_resolved()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(5432), Port(8080), Port(5432) });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(5432));

        var containerMappings = mapping.ToContainerPortMappings();
        Assert.Equal(2, containerMappings.Count);
        Assert.All(containerMappings, item => Assert.Equal((ushort)0, item.WindowsPort));
        Assert.All(containerMappings, item => Assert.Equal(Microsoft.WSL.Containers.PortProtocol.TCP, item.Protocol));
        Assert.Equal(new[] { 5432, 8080 }, containerMappings.Select(item => (int)item.ContainerPort).OrderBy(port => port));
    }

    [Fact]
    public void Bind_address_is_forwarded_to_the_container_mapping()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080, "127.0.0.1"), Port(9090) });

        var mappings = mapping.ToContainerPortMappings();
        var bound = Assert.Single(mappings, item => item.ContainerPort == 8080);
        var unbound = Assert.Single(mappings, item => item.ContainerPort == 9090);

        Assert.NotNull(bound.WindowsAddress);
        Assert.Equal("127.0.0.1", bound.WindowsAddress.RawName);
        Assert.Null(unbound.WindowsAddress);
    }

    [Theory]
    [InlineData(null, "127.0.0.1")]
    [InlineData("0.0.0.0", "127.0.0.1")]
    [InlineData("127.0.0.1", "127.0.0.1")]
    [InlineData("192.168.1.10", "192.168.1.10")]
    [InlineData("::", "::1")]
    [InlineData("::1", "::1")]
    public void Connect_endpoint_follows_the_bind_address(string? bindAddress, string expected)
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080, bindAddress) });
        mapping.ResolveFromInspect("""{"Ports":{"8080/tcp":[{"HostPort":"4514"}]}}""");

        var endpoint = mapping.GetConnectEndpoint(8080);
        Assert.Equal(expected, endpoint.Address.ToString());
        Assert.Equal(4514, endpoint.Port);
    }

    [Fact]
    public void Inspect_payload_resolves_dynamic_ports()
    {
        const string inspectJson =
            """{"Ports":{"8080/tcp":[{"HostIp":"127.0.0.1","HostPort":"4514"}],"5432/tcp":[{"HostIp":"127.0.0.1","HostPort":"4515"}]}}""";

        var mapping = WslcPortMapping.Create(new[] { Port(8080), Port(5432), Port(9090) });
        mapping.ResolveFromInspect(inspectJson);

        Assert.Equal(4514, mapping.GetMappedPort(8080));
        Assert.Equal(4515, mapping.GetMappedPort(5432));
        Assert.Equal(new[] { "9090" }, mapping.UnresolvedPorts);
    }

    [Fact]
    public void Inspect_payload_accepts_numeric_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });
        mapping.ResolveFromInspect("""{"Ports":{"8080/tcp":[{"HostPort":4514}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_ignores_invalid_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostPort":{}},{"HostPort":"abc"},{"HostPort":"70000"},{"HostPort":"4515"}]}}""");

        Assert.Equal(4515, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_rejects_lax_port_strings()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostPort":" 8080"},{"HostPort":"+8080"},{"HostPort":"4514.9"},{"HostPort":"8080.0"},{"HostPort":"0"},{"HostPort":"65536"},{"HostPort":"-1"},{"HostPort":"1e2"},{"HostPort":"4515"}]}}""");

        Assert.Equal(4515, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_rejects_fractional_numeric_host_ports()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/tcp":[{"HostPort":4514.9},{"HostPort":-1},{"HostPort":65536.0},{"HostPort":4515}]}}""");

        Assert.Equal(4515, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Inspect_payload_accepts_leading_zeros()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080), Port(9090) });
        mapping.ResolveFromInspect(
            """{"Ports":{"08080/tcp":[{"HostPort":"04514"}],"9090":[{"HostPort":" 9090"},{"HostPort":"9095"}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
        Assert.Equal(9095, mapping.GetMappedPort(9090));
    }

    [Fact]
    public void Inspect_payload_rejects_lax_port_keys()
    {
        var mapping = WslcPortMapping.Create(new[]
        {
            Port(8080), Port(8081), Port(8082), Port(8083), Port(8084), Port(8085),
        });
        mapping.ResolveFromInspect(
            """{"Ports":{" 8080/tcp":[{"HostPort":"4514"}],"+8081/tcp":[{"HostPort":"4515"}],"8082.0/tcp":[{"HostPort":"4516"}],"0/tcp":[{"HostPort":"4517"}],"65536/tcp":[{"HostPort":"4518"}],"-1/tcp":[{"HostPort":"4519"}]}}""");

        foreach (var port in new[] { 8080, 8081, 8082, 8083, 8084, 8085 })
        {
            Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(port));
        }
    }

    [Fact]
    public void Unknown_ports_throw()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });

        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(9090));
    }

    [Fact]
    public void Inspect_payload_handles_protocol_suffix_variants()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080), Port(8081), Port(8082) });
        mapping.ResolveFromInspect(
            """{"Ports":{"8080/":[{"HostPort":"4514"}],"8081/UDP":[{"HostPort":"4515"}],"8082/sctp":[{"HostPort":"4516"}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(8081));
        Assert.Throws<WslNetworkException>(() => mapping.GetMappedPort(8082));
    }

    [Fact]
    public void Inspect_payload_accepts_bare_port_keys()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080) });
        mapping.ResolveFromInspect("""{"Ports":{"8080":[{"HostPort":"4514"}]}}""");

        Assert.Equal(4514, mapping.GetMappedPort(8080));
    }

    [Fact]
    public void Duplicate_mappings_keep_the_first_bind_address()
    {
        var mapping = WslcPortMapping.Create(new[] { Port(8080, "127.0.0.1"), Port(8080, "0.0.0.0") });

        var entry = Assert.Single(mapping.ToContainerPortMappings());
        Assert.NotNull(entry.WindowsAddress);
        Assert.Equal("127.0.0.1", entry.WindowsAddress.RawName);
    }

    [Fact]
    public async Task Open_host_ports_are_detected_and_closed_ports_are_not()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;

        try
        {
            var mapping = WslcPortMapping.Create(new[] { Port(8080) });
            mapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + port + "\"}]}}");

            Assert.True(await mapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
        }
        finally
        {
            listener.Stop();
        }

        // Hold an IPv6 listener open and probe its port over IPv4 loopback: the port is
        // never freed for reuse, so the "closed" assertion cannot race another process.
        var closedListener = new TcpListener(IPAddress.IPv6Loopback, 0);
        closedListener.Start();
        try
        {
            var closedPort = ((IPEndPoint)closedListener.LocalEndpoint).Port;
            var closedMapping = WslcPortMapping.Create(new[] { Port(8080) });
            closedMapping.ResolveFromInspect("{\"Ports\":{\"8080/tcp\":[{\"HostPort\":\"" + closedPort + "\"}]}}");

            Assert.False(await closedMapping.IsPortOpenAsync(8080, TestContext.Current.CancellationToken));
        }
        finally
        {
            closedListener.Stop();
        }
    }
}
