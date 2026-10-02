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
/// instead of accumulating stale rules).
/// </summary>
internal static class EgressIsolation
{
    /// <summary>
    /// Validates the options and returns an equivalent copy with hosts normalized to
    /// canonical IPv4/CIDR text and hosts/ports de-duplicated and sorted, so identical
    /// intent always produces the same rules and configuration hash.
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

        return options with
        {
            AllowedHosts = hosts.ToArray(),
            AllowedTcpPorts = ports.ToArray(),
        };
    }

    /// <summary>Builds the <c>sh</c> script applied with <c>sh -c</c> inside the container.</summary>
    public static string BuildScript(EgressAllowlistOptions options)
    {
        var normalized = Normalize(options);

        var script = new StringBuilder(1024);
        script.Append("set -eu\n");
        script.Append("command -v iptables >/dev/null 2>&1 || { echo 'wslc: iptables not found in container (image must provide iptables to use egress allowlisting)' >&2; exit 3; }\n");
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
            foreach (var port in ports)
            {
                script.Append("iptables -A OUTPUT -p tcp --dport ").Append(port).Append(" -j ACCEPT\n");
            }
        }
        else if (ports.Count == 0)
        {
            foreach (var host in hosts)
            {
                script.Append("iptables -A OUTPUT -p tcp -d ").Append(host).Append(" -j ACCEPT\n");
            }
        }
        else
        {
            foreach (var host in hosts)
            {
                foreach (var port in ports)
                {
                    script.Append("iptables -A OUTPUT -p tcp -d ").Append(host).Append(" --dport ").Append(port).Append(" -j ACCEPT\n");
                }
            }
        }

        // Best effort: only touch ip6tables when it exists and can initialize its tables,
        // otherwise a v6-less kernel would fail the whole script after v4 was applied.
        script.Append("if command -v ip6tables >/dev/null 2>&1 && ip6tables -L OUTPUT >/dev/null 2>&1; then\n");
        script.Append("  ip6tables -P OUTPUT DROP\n");
        script.Append("  ip6tables -F OUTPUT\n");
        script.Append("  ip6tables -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT\n");
        if (normalized.AllowLoopback)
        {
            script.Append("  ip6tables -A OUTPUT -o lo -j ACCEPT\n");
        }

        script.Append("fi\n");
        return script.ToString();
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
