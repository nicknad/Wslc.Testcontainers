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
    public void Script_requires_ip6tables_before_applying_anything()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions());

        var iptablesCheck = script.IndexOf("command -v iptables", StringComparison.Ordinal);
        var ip6tablesCheck = script.IndexOf("command -v ip6tables", StringComparison.Ordinal);
        var ip6tablesProbe = script.IndexOf("ip6tables -L OUTPUT >/dev/null 2>&1", StringComparison.Ordinal);
        var v4Policy = script.IndexOf("iptables -P OUTPUT DROP", StringComparison.Ordinal);
        var v6Policy = script.IndexOf("ip6tables -P OUTPUT DROP", StringComparison.Ordinal);

        Assert.True(iptablesCheck >= 0, "iptables presence must be checked");
        Assert.True(ip6tablesCheck > iptablesCheck, "ip6tables presence must be checked");
        Assert.True(ip6tablesProbe > ip6tablesCheck, "ip6tables tables must be probed");
        Assert.True(v4Policy > ip6tablesProbe, "IPv6 must be verified before IPv4 is modified so a v6 failure applies nothing");
        Assert.True(v6Policy > v4Policy, "IPv6 drop must be applied");
        Assert.Contains("exit 4", script);
        Assert.Contains("ip6tables -F OUTPUT", script);
        Assert.Contains("ip6tables -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT", script);
    }

    [Fact]
    public void Script_drops_ipv6_unconditionally_once_validated()
    {
        var strict = EgressIsolation.BuildScript(new EgressAllowlistOptions { AllowLoopback = false });

        Assert.Contains("ip6tables -P OUTPUT DROP", strict);
        Assert.DoesNotContain("ip6tables -A OUTPUT -o lo -j ACCEPT", strict);
        Assert.DoesNotContain("if command -v ip6tables", strict);
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
        Assert.Equal(2, CountOccurrences(script, "iptables -A OUTPUT -p tcp -d "));
        Assert.Equal(1, CountOccurrences(script, "10.0.0.0/8 -m multiport --dports 80,443 -j ACCEPT"));
        Assert.Equal(1, CountOccurrences(script, "10.0.0.5 -m multiport --dports 80,443 -j ACCEPT"));
    }

    [Fact]
    public void Script_batches_ports_into_multiport_rules()
    {
        var script = EgressIsolation.BuildScript(new EgressAllowlistOptions
        {
            AllowedHosts = new[] { "10.0.0.5" },
            AllowedTcpPorts = Enumerable.Range(1, 16).ToArray(),
        });

        Assert.Equal(2, CountOccurrences(script, "iptables -A OUTPUT -p tcp -d 10.0.0.5 -m multiport --dports "));
        Assert.Contains("--dports 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 -j ACCEPT", script);
        Assert.Contains("--dports 16 -j ACCEPT", script);
    }

    [Fact]
    public void Normalize_rejects_allowlists_that_exceed_the_rule_limit()
    {
        var hosts = Enumerable.Range(1, 300).Select(index => $"10.0.{index / 256}.{index % 256}").ToArray();

        Assert.Throws<ArgumentException>(() => EgressIsolation.Normalize(new EgressAllowlistOptions
        {
            AllowedHosts = hosts,
        }));
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
