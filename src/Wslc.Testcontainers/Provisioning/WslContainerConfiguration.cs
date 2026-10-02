using System.Buffers.Binary;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Provisioning;

/// <summary>Immutable description of the container requested through the builder.</summary>
internal sealed record WslContainerConfiguration
{
    public string? Image { get; init; }

    public string? TarballPath { get; init; }

    public string? TarballImageName { get; init; }

    public string? Command { get; init; }

    public IReadOnlyList<string> CommandArguments { get; init; } = Array.Empty<string>();

    public string? WorkingDirectory { get; init; }

    public IReadOnlyDictionary<string, string> Environment { get; init; } = new Dictionary<string, string>(StringComparer.Ordinal);

    public IReadOnlyList<WslPortMapping> PortMappings { get; init; } = Array.Empty<WslPortMapping>();

    public IReadOnlyList<IWaitStrategy> WaitStrategies { get; init; } = Array.Empty<IWaitStrategy>();

    public IReadOnlyList<WslFileCopy> Files { get; init; } = Array.Empty<WslFileCopy>();

    public IReadOnlyList<WslVolumeMount> Volumes { get; init; } = Array.Empty<WslVolumeMount>();

    public IReadOnlyList<WslSessionVolume> SessionVolumes { get; init; } = Array.Empty<WslSessionVolume>();

    /// <summary>Container network mode. Null means the runtime default (bridged).</summary>
    public ContainerNetworkMode? NetworkingMode { get; init; }

    /// <summary>Session CPU count cap. Null leaves the runtime default.</summary>
    public uint? CpuCount { get; init; }

    /// <summary>Session memory cap in megabytes. Null leaves the runtime default.</summary>
    public uint? MemorySizeInMB { get; init; }

    /// <summary>Egress allowlist applied after start. Null means unrestricted egress.</summary>
    public EgressAllowlistOptions? EgressAllowlist { get; init; }

    public bool? Reuse { get; init; }

    public TimeSpan StartupTimeout { get; init; } = TimeSpan.FromSeconds(120);
}

internal sealed record WslFileCopy(string Source, string Destination);

internal sealed record WslVolumeMount(string HostPath, string ContainerPath, bool ReadOnly);

/// <summary>A Linux port exposed on a dynamic Windows port.</summary>
/// <param name="ContainerPort">Linux service port (1-65535).</param>
/// <param name="Protocol">TCP or UDP.</param>
/// <param name="BindAddress">Optional normalized Windows bind address (e.g. 127.0.0.1). Null uses the SDK default (loopback).</param>
internal sealed record WslPortMapping(int ContainerPort, PortProtocol Protocol, string? BindAddress)
{
    /// <summary>Formats a port/protocol pair as <c>port</c> (TCP) or <c>port/udp</c>.</summary>
    public static string Format(int port, PortProtocol protocol) =>
        protocol == PortProtocol.Udp
            ? string.Concat(port.ToString(CultureInfo.InvariantCulture), "/udp")
            : port.ToString(CultureInfo.InvariantCulture);

    public override string ToString() => Format(ContainerPort, Protocol);
}

/// <summary>A session VHD volume mounted into the container. Recreated empty on every start.</summary>
/// <param name="Name">Session volume name.</param>
/// <param name="ContainerPath">Absolute Linux mount path.</param>
/// <param name="ReadOnly">Mount read-only.</param>
/// <param name="SizeBytes">VHD size in bytes (must be positive).</param>
/// <param name="Type">Dynamic (default) or fixed allocation.</param>
internal sealed record WslSessionVolume(string Name, string ContainerPath, bool ReadOnly, ulong SizeBytes, VhdAllocationType Type);

/// <summary>Generates collision-free session names and recognizes WSLC-owned resources.</summary>
internal static class WslNaming
{
    public const string Prefix = "wslc-";

    public static string CreateInstanceName(string sessionId) =>
        $"{Prefix}{Slug(sessionId)}-{RandomHex(8)}";

    public static string CreateReuseName(string configHash) =>
        $"{Prefix}reuse-{configHash[..12]}";

    public static bool IsManaged(string name) =>
        name.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase);

    public static string Slug(string value, int maxLength = 24)
    {
        var builder = new StringBuilder(Math.Min(value.Length, maxLength + 8));
        foreach (var character in value)
        {
            if (char.IsLetterOrDigit(character))
            {
                builder.Append(char.ToLowerInvariant(character));
            }
            else if (builder.Length > 0 && builder[^1] != '-')
            {
                builder.Append('-');
            }
        }

        var slug = builder.ToString().Trim('-');
        if (slug.Length == 0)
        {
            slug = "session";
        }

        return slug.Length <= maxLength ? slug : slug.Substring(0, maxLength);
    }

    private static string RandomHex(int length) =>
        Convert.ToHexString(RandomNumberGenerator.GetBytes((length + 1) / 2)).ToLowerInvariant().Substring(0, length);
}

/// <summary>Computes a stable identity for reusable environments.</summary>
internal static class WslConfigHasher
{
    public static string Compute(WslContainerConfiguration configuration)
    {
        // Length-prefixed binary encoding: the previous newline/separator text encoding was
        // not injective, so a value containing a delimiter could hash the same as two
        // separate fields and reuse the wrong instance. Prefixing every field with its byte
        // count removes that ambiguity, and null is encoded distinctly from an empty string.
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);

        WriteString(hash, configuration.Image);
        WriteString(hash, configuration.TarballPath);
        WriteString(hash, configuration.TarballImageName);
        WriteString(hash, configuration.Command);

        WriteInt32(hash, configuration.CommandArguments.Count);
        foreach (var argument in configuration.CommandArguments)
        {
            WriteString(hash, argument);
        }

        WriteString(hash, configuration.WorkingDirectory);

        // Sort in place to avoid OrderBy sorter allocations on the reuse path.
        var env = configuration.Environment.ToArray();
        Array.Sort(env, static (a, b) => string.CompareOrdinal(a.Key, b.Key));
        WriteInt32(hash, env.Length);
        foreach (var pair in env)
        {
            WriteString(hash, pair.Key);
            WriteString(hash, pair.Value);
        }

        var ports = configuration.PortMappings.ToArray();
        Array.Sort(ports, static (a, b) =>
        {
            var c = a.ContainerPort.CompareTo(b.ContainerPort);
            if (c != 0)
            {
                return c;
            }

            c = ((int)a.Protocol).CompareTo((int)b.Protocol);
            return c != 0 ? c : string.CompareOrdinal(a.BindAddress, b.BindAddress);
        });
        WriteInt32(hash, ports.Length);
        foreach (var port in ports)
        {
            WriteInt32(hash, port.ContainerPort);
            WriteInt32(hash, (int)port.Protocol);
            WriteString(hash, port.BindAddress);
        }

        WriteInt32(hash, configuration.NetworkingMode.HasValue ? (int)configuration.NetworkingMode.Value : -1);
        WriteUInt32(hash, configuration.CpuCount);
        WriteUInt32(hash, configuration.MemorySizeInMB);

        var files = configuration.Files.ToArray();
        Array.Sort(files, static (a, b) =>
        {
            var c = string.CompareOrdinal(a.Destination, b.Destination);
            return c != 0 ? c : string.CompareOrdinal(a.Source, b.Source);
        });
        WriteInt32(hash, files.Length);
        foreach (var file in files)
        {
            WriteString(hash, file.Source);
            WriteString(hash, file.Destination);
            WriteFileContentHash(hash, file.Source);
        }

        var volumes = configuration.Volumes.ToArray();
        Array.Sort(volumes, static (a, b) =>
        {
            var c = string.CompareOrdinal(a.ContainerPath, b.ContainerPath);
            return c != 0 ? c : string.CompareOrdinal(a.HostPath, b.HostPath);
        });
        WriteInt32(hash, volumes.Length);
        Span<byte> readOnlyMarker = stackalloc byte[1];
        foreach (var volume in volumes)
        {
            WriteString(hash, volume.HostPath);
            WriteString(hash, volume.ContainerPath);
            readOnlyMarker[0] = volume.ReadOnly ? (byte)1 : (byte)0;
            hash.AppendData(readOnlyMarker);
        }

        var sessionVolumes = configuration.SessionVolumes.ToArray();
        Array.Sort(sessionVolumes, static (a, b) =>
        {
            var c = string.CompareOrdinal(a.ContainerPath, b.ContainerPath);
            return c != 0 ? c : string.CompareOrdinal(a.Name, b.Name);
        });
        WriteInt32(hash, sessionVolumes.Length);
        foreach (var volume in sessionVolumes)
        {
            WriteString(hash, volume.Name);
            WriteString(hash, volume.ContainerPath);
            readOnlyMarker[0] = volume.ReadOnly ? (byte)1 : (byte)0;
            hash.AppendData(readOnlyMarker);
            WriteUInt64(hash, volume.SizeBytes);
            WriteInt32(hash, (int)volume.Type);
        }

        WriteEgressAllowlist(hash, configuration.EgressAllowlist);

        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void WriteEgressAllowlist(IncrementalHash hash, EgressAllowlistOptions? egress)
    {
        if (egress is null)
        {
            WriteInt32(hash, -1);
            return;
        }

        var hosts = egress.AllowedHosts.ToArray();
        Array.Sort(hosts, StringComparer.Ordinal);
        WriteInt32(hash, hosts.Length);
        foreach (var host in hosts)
        {
            WriteString(hash, host);
        }

        var ports = egress.AllowedTcpPorts.ToArray();
        Array.Sort(ports);
        WriteInt32(hash, ports.Length);
        foreach (var port in ports)
        {
            WriteInt32(hash, port);
        }

        Span<byte> flags = stackalloc byte[2];
        flags[0] = egress.AllowDns ? (byte)1 : (byte)0;
        flags[1] = egress.AllowLoopback ? (byte)1 : (byte)0;
        hash.AppendData(flags);
    }

    private static void WriteString(IncrementalHash hash, string? value)
    {
        if (value is null)
        {
            WriteInt32(hash, -1);
            return;
        }

        var byteCount = Encoding.UTF8.GetByteCount(value);
        WriteInt32(hash, byteCount);
        if (byteCount == 0)
        {
            return;
        }

        hash.AppendData(Encoding.UTF8.GetBytes(value));
    }

    private static void WriteInt32(IncrementalHash hash, int value)
    {
        Span<byte> buffer = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(buffer, value);
        hash.AppendData(buffer);
    }

    private static void WriteUInt32(IncrementalHash hash, uint? value)
    {
        if (value is null)
        {
            WriteInt32(hash, -1);
            return;
        }

        Span<byte> buffer = stackalloc byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32LittleEndian(buffer, value.Value);
        hash.AppendData(buffer);
    }

    private static void WriteUInt64(IncrementalHash hash, ulong value)
    {
        Span<byte> buffer = stackalloc byte[sizeof(ulong)];
        BinaryPrimitives.WriteUInt64LittleEndian(buffer, value);
        hash.AppendData(buffer);
    }

    private static void WriteFileContentHash(IncrementalHash hash, string path)
    {
        if (!File.Exists(path))
        {
            hash.AppendData(stackalloc byte[1] { 0 });
            return;
        }

        hash.AppendData(stackalloc byte[1] { 1 });
        using var stream = File.OpenRead(path);
        var contentHash = SHA256.HashData(stream);
        hash.AppendData(contentHash);
    }
}
