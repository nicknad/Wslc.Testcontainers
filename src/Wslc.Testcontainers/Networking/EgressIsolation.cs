using System.Buffers.Binary;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Wslc.Testcontainers.Networking;

/// <summary>
/// Validates and normalizes <see cref="EgressAllowlistOptions"/> and renders the
/// <c>iptables</c> script applied inside the container. Hosts are restricted to
/// validated IPv4 literals/CIDRs so rule text can never inject shell metacharacters.
/// Applying the script replaces the whole <c>OUTPUT</c> chain: it is fail-closed
/// (policy set first) and idempotent (re-applying narrows or widens deterministically
/// instead of accumulating stale rules). IPv6 is fail-closed too: the script refuses
/// to apply anything unless <c>ip6tables</c> is present and can manage its tables, so
/// a container with an IPv6 route can never silently keep v6 egress open.
/// </summary>
internal static class EgressIsolation
{
    // iptables multiport accepts at most 15 ports per rule. Batching keeps both the
    // number of iptables subprocesses and the script size bounded.
    private const int MaxPortsPerMultiportRule = 15;

    // Cap on generated ACCEPT rules (not on hosts/ports): bounds rule installation time
    // and keeps the script safely below the exec argument size limit.
    private const int MaxGeneratedRules = 256;

    /// <summary>
    /// Validates the options and returns an equivalent copy with hosts normalized to
    /// canonical IPv4/CIDR text and hosts/ports de-duplicated and sorted, so identical
    /// intent always produces the same rules and configuration hash. Throws when the
    /// allowlist would generate more than the supported number of iptables rules.
    /// </summary>
    public static EgressAllowlistOptions Normalize(EgressAllowlistOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(options.AllowedHosts);
        ArgumentNullException.ThrowIfNull(options.AllowedTcpPorts);

        var hosts = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var host in options.AllowedHosts)
        {
            if (!TryParseIPv4Host(host, out var normalized))
            {
                throw new ArgumentException(
                    $"Egress allowlist host '{host}' must be an IPv4 literal (e.g. 10.0.0.5) or IPv4 CIDR (e.g. 10.0.0.0/8). DNS names and IPv6 are not supported.",
                    nameof(options));
            }

            hosts.Add(normalized);
        }

        var ports = new SortedSet<int>();
        foreach (var port in options.AllowedTcpPorts)
        {
            if (port is < 1 or > 65535)
            {
                throw new ArgumentOutOfRangeException(nameof(options), port, "Egress allowlist ports must be 1-65535.");
            }

            ports.Add(port);
        }

        ValidateRuleCount(hosts.Count, ports.Count, options);

        return options with
        {
            AllowedHosts = hosts.ToArray(),
            AllowedTcpPorts = ports.ToArray(),
        };
    }

    private static void ValidateRuleCount(int hostCount, int portCount, EgressAllowlistOptions options)
    {
        var portBatches = portCount == 0 ? 0 : (portCount + MaxPortsPerMultiportRule - 1) / MaxPortsPerMultiportRule;
        var ruleCount = (hostCount, portCount) switch
        {
            (0, 0) => 0,
            (0, _) => portBatches,
            (_, 0) => hostCount,
            _ => hostCount * portBatches,
        };

        if (ruleCount > MaxGeneratedRules)
        {
            throw new ArgumentException(
                $"Egress allowlist would generate {ruleCount} iptables rules, exceeding the limit of {MaxGeneratedRules}. " +
                $"Reduce the number of hosts ({hostCount}) or ports ({portCount}); ports are grouped up to {MaxPortsPerMultiportRule} per rule.",
                nameof(options));
        }
    }

    /// <summary>Builds the <c>sh</c> script applied with <c>sh -c</c> inside the container.</summary>
    public static string BuildScript(EgressAllowlistOptions options)
    {
        var normalized = Normalize(options);

        var script = new StringBuilder(1024);
        script.Append("set -eu\n");
        script.Append("command -v iptables >/dev/null 2>&1 || { echo 'wslc: iptables not found in container (image must provide iptables to use egress allowlisting)' >&2; exit 3; }\n");
        // Fail closed on IPv6 before touching IPv4: without a usable ip6tables the
        // container can still egress over v6, so refuse to apply a partial policy
        // rather than leaving an unnoticed v6 hole. Probes are read-only.
        script.Append("command -v ip6tables >/dev/null 2>&1 || { echo 'wslc: ip6tables not found in container (the image must provide ip6tables so IPv6 egress can be dropped; install it or use NetworkingMode.None)' >&2; exit 4; }\n");
        script.Append("ip6tables -L OUTPUT >/dev/null 2>&1 || { echo 'wslc: ip6tables cannot manage IPv6 rules in this container (IPv6 egress cannot be dropped; use NetworkingMode.None)' >&2; exit 4; }\n");
        // Fail closed: drop the policy, clear the chain, then install the allowed rules.
        // Clearing makes re-application replace the previous policy instead of unioning
        // with it (including any ACCEPT rules the image installed in OUTPUT).
        script.Append("iptables -P OUTPUT DROP\n");
        script.Append("iptables -F OUTPUT\n");
        script.Append("iptables -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT\n");
        if (normalized.AllowLoopback)
        {
            script.Append("iptables -A OUTPUT -o lo -j ACCEPT\n");
            script.Append("iptables -A OUTPUT -d 127.0.0.0/8 -j ACCEPT\n");
        }

        if (normalized.AllowDns)
        {
            script.Append("iptables -A OUTPUT -p udp --dport 53 -j ACCEPT\n");
            script.Append("iptables -A OUTPUT -p tcp --dport 53 -j ACCEPT\n");
        }

        var hosts = normalized.AllowedHosts;
        var ports = normalized.AllowedTcpPorts;
        if (hosts.Count == 0)
        {
            AppendPortRules(script, host: null, ports);
        }
        else
        {
            foreach (var host in hosts)
            {
                AppendPortRules(script, host, ports);
            }
        }

        // ip6tables presence and table access were verified above. IPv6 has no host
        // allowlist (hosts are IPv4-only), so only established traffic and loopback
        // are kept, matching the v4 default-deny posture.
        script.Append("ip6tables -P OUTPUT DROP\n");
        script.Append("ip6tables -F OUTPUT\n");
        script.Append("ip6tables -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT\n");
        if (normalized.AllowLoopback)
        {
            script.Append("ip6tables -A OUTPUT -o lo -j ACCEPT\n");
        }

        return script.ToString();
    }

    /// <summary>
    /// Emits one rule per host when no ports are listed, a plain <c>--dport</c> rule for
    /// a single port, and chunks of up to <see cref="MaxPortsPerMultiportRule"/> ports in
    /// a <c>multiport</c> rule otherwise. The total number of emitted rules is bounded by
    /// <see cref="MaxGeneratedRules"/>, which <see cref="Normalize"/> enforces.
    /// </summary>
    private static void AppendPortRules(StringBuilder script, string? host, IReadOnlyList<int> ports)
    {
        if (ports.Count == 0)
        {
            if (host is not null)
            {
                script.Append("iptables -A OUTPUT -p tcp -d ").Append(host).Append(" -j ACCEPT\n");
            }

            return;
        }

        if (ports.Count == 1)
        {
            AppendSinglePortRule(script, host, ports[0]);
            return;
        }

        for (var start = 0; start < ports.Count; start += MaxPortsPerMultiportRule)
        {
            script.Append("iptables -A OUTPUT -p tcp ");
            if (host is not null)
            {
                script.Append("-d ").Append(host).Append(' ');
            }

            script.Append("-m multiport --dports ");
            AppendPortList(script, ports, start);
            script.Append(" -j ACCEPT\n");
        }
    }

    private static void AppendSinglePortRule(StringBuilder script, string? host, int port)
    {
        script.Append("iptables -A OUTPUT -p tcp ");
        if (host is not null)
        {
            script.Append("-d ").Append(host).Append(' ');
        }

        script.Append("--dport ").Append(port).Append(" -j ACCEPT\n");
    }

    private static void AppendPortList(StringBuilder script, IReadOnlyList<int> ports, int start)
    {
        var end = Math.Min(start + MaxPortsPerMultiportRule, ports.Count);
        for (var index = start; index < end; index++)
        {
            if (index > start)
            {
                script.Append(',');
            }

            script.Append(ports[index]);
        }
    }

    private static bool TryParseIPv4Host(string? value, out string normalized)
    {
        normalized = string.Empty;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var slash = value.IndexOf('/');
        if (slash < 0)
        {
            if (!IPAddress.TryParse(value, out var address) || address.AddressFamily != AddressFamily.InterNetwork)
            {
                return false;
            }

            normalized = address.ToString();
            return true;
        }

        // CIDR: address/prefix with no extra slashes or zones.
        if (slash == 0 || slash != value.LastIndexOf('/'))
        {
            return false;
        }

        var addressPart = value.Substring(0, slash);
        var prefixPart = value.Substring(slash + 1);
        if (!IPAddress.TryParse(addressPart, out var network) || network.AddressFamily != AddressFamily.InterNetwork)
        {
            return false;
        }

        if (!int.TryParse(prefixPart, NumberStyles.None, CultureInfo.InvariantCulture, out var prefix) ||
            prefix is < 0 or > 32)
        {
            return false;
        }

        // Canonicalize to the network address so 10.1.2.3/8 and 10.0.0.0/8 collapse.
        var bytes = network.GetAddressBytes();
        var mask = prefix == 0 ? 0u : uint.MaxValue << (32 - prefix);
        BinaryPrimitives.WriteUInt32BigEndian(bytes, BinaryPrimitives.ReadUInt32BigEndian(bytes) & mask);
        normalized = string.Concat(new IPAddress(bytes).ToString(), "/", prefix.ToString(CultureInfo.InvariantCulture));
        return true;
    }
}
