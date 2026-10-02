using Wslc.Testcontainers.Networking;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class EgressIsolationTests
{
    [Fact]
    public void Script_allows_endpoints_and_drops_the_rest()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedHosts = new[] { "10.0.0.5" },
            AllowedTcpPorts = new[] { 443 },
        });

        Assert.Contains("iptables -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT", script);
        Assert.Contains("iptables -A OUTPUT -o lo -j ACCEPT", script);
        Assert.Contains("iptables -A OUTPUT -p udp --dport 53 -j ACCEPT", script);
        Assert.Contains("iptables -A OUTPUT -p tcp -d 10.0.0.5 --dport 443 -j ACCEPT", script);
        Assert.Contains("iptables -P OUTPUT DROP", script);
        Assert.Contains("ip6tables -P OUTPUT DROP", script);
    }

    [Fact]
    public void Script_supports_cidr_and_any_port_hosts()
    {
        var cidr = EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedHosts = new[] { "192.168.0.0/16" },
        });

        Assert.Contains("iptables -A OUTPUT -p tcp -d 192.168.0.0/16 -j ACCEPT", cidr);

        var portsOnly = EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedTcpPorts = new[] { 80 },
        });

        Assert.Contains("iptables -A OUTPUT -p tcp --dport 80 -j ACCEPT", portsOnly);
    }

    [Fact]
    public void Script_honors_dns_and_loopback_flags()
    {
        var strict = EgressIsolation.BuildScript(new EgressAllowlistOptions { AllowDns = false, AllowLoopback = false });

        Assert.DoesNotContain("--dport 53", strict);
        Assert.DoesNotContain("-o lo", strict);
        Assert.Contains("iptables -P OUTPUT DROP", strict);
    }

    [Fact]
    public void Script_fails_closed_and_replaces_previous_rules()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions());

        var policy = script.IndexOf("iptables -P OUTPUT DROP", StringComparison.Ordinal);
        var flush = script.IndexOf("iptables -F OUTPUT", StringComparison.Ordinal);
        var established = script.IndexOf("iptables -A OUTPUT -m conntrack", StringComparison.Ordinal);

        Assert.True(policy >= 0, "policy must be set");
        Assert.True(flush > policy, "the chain must be cleared after the policy is set (fail closed)");
        Assert.True(established > flush, "allow rules must be installed after the flush");
    }

    [Fact]
    public void Script_gates_ip6tables_on_usable_tables()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions());

        Assert.Contains("ip6tables -L OUTPUT >/dev/null 2>&1", script);
        Assert.Contains("ip6tables -F OUTPUT", script);
    }

    [Fact]
    public void Script_normalizes_de_duplicates_and_sorts_hosts_and_ports()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedHosts = new[] { "10.1.2.3/8", "10.0.0.0/8", "10.0.0.5", "10.0.0.5" },
            AllowedTcpPorts = new[] { 443, 80, 443 },
        });

        Assert.DoesNotContain("10.1.2.3", script);
        Assert.Equal(4, CountOccurrences(script, "iptables -A OUTPUT -p tcp -d "));
        Assert.Equal(2, CountOccurrences(script, "10.0.0.0/8 --dport"));
        Assert.Equal(2, CountOccurrences(script, "10.0.0.5 --dport"));
        Assert.Equal(2, CountOccurrences(script, "--dport 80 -j ACCEPT"));
        Assert.Equal(2, CountOccurrences(script, "--dport 443 -j ACCEPT"));
    }

    [Fact]
    public void Normalize_returns_a_detached_canonical_copy()
    {
        var original = new EgressAllowlistOptions
        {
            AllowedHosts = new[] { "10.1.2.3/8", "10.0.0.5" },
            AllowedTcpPorts = new[] { 443, 80, 443 },
        };

        var normalized = EgressIsolation.Normalize(original);

        Assert.NotSame(original, normalized);
        Assert.Equal(new[] { "10.0.0.0/8", "10.0.0.5" }, normalized.AllowedHosts);
        Assert.Equal(new[] { 80, 443 }, normalized.AllowedTcpPorts);
    }

    private static int CountOccurrences(string text, string value)
    {
        var count = 0;
        var index = 0;
        while ((index = text.IndexOf(value, index, StringComparison.Ordinal)) >= 0)
        {
            count++;
            index += value.Length;
        }

        return count;
    }

    [Theory]
    [InlineData("example.com")]
    [InlineData("10.0.0.5:443")]
    [InlineData("::1")]
    [InlineData("2001:db8::/32")]
    [InlineData("10.0.0.0/33")]
    [InlineData("10.0.0.0/-1")]
    [InlineData("")]
    [InlineData("10.0.0.5; rm -rf /")]
    public void Invalid_hosts_are_rejected(string host)
    {
        Assert.Throws<ArgumentException>(() => EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedHosts = new[] { host },
        }));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(70000)]
    [InlineData(-1)]
    public void Invalid_ports_are_rejected(int port)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedTcpPorts = new[] { port },
        }));
    }
}
