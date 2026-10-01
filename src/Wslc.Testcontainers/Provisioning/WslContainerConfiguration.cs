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

    public IReadOnlyList<int> Ports { get; init; } = Array.Empty<int>();

    public IReadOnlyList<IWaitStrategy> WaitStrategies { get; init; } = Array.Empty<IWaitStrategy>();

    public IReadOnlyList<WslFileCopy> Files { get; init; } = Array.Empty<WslFileCopy>();

    public IReadOnlyList<WslVolumeMount> Volumes { get; init; } = Array.Empty<WslVolumeMount>();

    public bool? Reuse { get; init; }

    public TimeSpan StartupTimeout { get; init; } = TimeSpan.FromSeconds(120);
}

internal sealed record WslFileCopy(string Source, string Destination);

internal sealed record WslVolumeMount(string HostPath, string ContainerPath, bool ReadOnly);

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
        var builder = new StringBuilder(512);
        builder.Append("image=").AppendLine(configuration.Image);
        builder.Append("tarball=").AppendLine(configuration.TarballPath);
        builder.Append("tarballImage=").AppendLine(configuration.TarballImageName);
        builder.Append("command=").Append(configuration.Command).Append('\u001f').AppendLine(string.Join('\u001f', configuration.CommandArguments));
        builder.Append("cwd=").AppendLine(configuration.WorkingDirectory);

        // Sort in place to avoid OrderBy sorter allocations on hot reuse path.
        var env = configuration.Environment.ToArray();
        Array.Sort(env, static (a, b) => string.CompareOrdinal(a.Key, b.Key));
        foreach (var pair in env)
        {
            builder.Append("env:").Append(pair.Key).Append('=').AppendLine(pair.Value);
        }

        var ports = configuration.Ports.ToArray();
        Array.Sort(ports);
        foreach (var port in ports)
        {
            builder.Append("port:").AppendLine(port.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }

        var files = configuration.Files.ToArray();
        Array.Sort(files, static (a, b) =>
        {
            var c = string.CompareOrdinal(a.Destination, b.Destination);
            return c != 0 ? c : string.CompareOrdinal(a.Source, b.Source);
        });
        foreach (var file in files)
        {
            builder.Append("file:").Append(file.Source).Append("->").AppendLine(file.Destination);
            if (File.Exists(file.Source))
            {
                builder.Append("sha=").AppendLine(HashFile(file.Source));
            }
        }

        var volumes = configuration.Volumes.ToArray();
        Array.Sort(volumes, static (a, b) =>
        {
            var c = string.CompareOrdinal(a.ContainerPath, b.ContainerPath);
            return c != 0 ? c : string.CompareOrdinal(a.HostPath, b.HostPath);
        });
        foreach (var volume in volumes)
        {
            builder.Append("volume:").Append(volume.HostPath).Append("->").Append(volume.ContainerPath)
                .Append(" ro=").AppendLine(volume.ReadOnly ? "1" : "0");
        }

        var bytes = Encoding.UTF8.GetBytes(builder.ToString());
        return Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    }

    private static string HashFile(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }
}
