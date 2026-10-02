namespace Wslc.Testcontainers;

/// <summary>
/// Egress firewall applied inside the container after start. The container keeps
/// only the listed TCP destinations (plus loopback/DNS as configured); all other
/// egress is dropped via <c>iptables</c>.
/// </summary>
/// <remarks>
/// The image must provide <c>iptables</c>, and a usable <c>ip6tables</c> whenever the
/// container has IPv6 addresses, and the container needs <c>CAP_NET_ADMIN</c>; applying
/// fails closed (without installing any rule) when a required tool is unavailable or
/// cannot manage rules, so egress can never be silently left open. Containers without
/// IPv6 addresses (empty/absent <c>/proc/net/if_inet6</c>) skip the v6 rules. WSL 3.0.1
/// containers are not granted <c>CAP_NET_ADMIN</c>, so on that runtime applying
/// consistently fails with guidance. Ingress (host-to-container port mappings and
/// readiness probes) is unaffected. Applying replaces the container's whole
/// <c>OUTPUT</c> chain, so re-applying narrows or widens deterministically.
/// Matching is the cartesian product of hosts and ports: each allowed host may be
/// reached on each allowed port. Empty hosts with non-empty ports allows those ports
/// anywhere; both empty isolates the container except for DNS/loopback (see
/// <see cref="AllowDns"/>, <see cref="AllowLoopback"/>). Hosts must be IPv4 literals
/// or IPv4 CIDRs (no DNS names — they would be resolved by the very network path
/// being restricted, and would widen the rule); they are normalized and de-duplicated
/// when the container is built. Ports are grouped into <c>multiport</c> rules, and an
/// allowlist that would generate more than 256 rules is rejected. Root inside the
/// container can remove the rules, so this is egress hygiene, not a tamper-proof
/// security boundary.
/// </remarks>
public sealed record EgressAllowlistOptions
{
    /// <summary>Allowed destination hosts as IPv4 literals or CIDRs (e.g. <c>10.0.0.5</c>, <c>10.0.0.0/8</c>).</summary>
    public IReadOnlyList<string> AllowedHosts { get; init; } = Array.Empty<string>();

    /// <summary>Allowed destination TCP ports (1-65535). Empty allows any TCP port to <see cref="AllowedHosts"/>.</summary>
    public IReadOnlyList<int> AllowedTcpPorts { get; init; } = Array.Empty<int>();

    /// <summary>Allow UDP+TCP port 53 egress for name resolution. Disable for strict IP-only containment.</summary>
    public bool AllowDns { get; init; } = true;

    /// <summary>Allow loopback traffic (<c>lo</c> and <c>127.0.0.0/8</c>).</summary>
    public bool AllowLoopback { get; init; } = true;
}
