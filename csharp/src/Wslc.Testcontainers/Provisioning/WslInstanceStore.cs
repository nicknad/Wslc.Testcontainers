using System.Text.Json;

namespace Wslc.Testcontainers.Provisioning;

/// <summary>Owns the on-disk WSLC data directory: per-instance session storage and metadata.</summary>
internal sealed class WslInstanceStore
{
    private const string MetadataFileName = "wslc.json";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private static readonly Lazy<WslInstanceStore> DefaultStore = new(
        () => new WslInstanceStore(WslEnvironment.DataDirectory, WslEnvironment.SessionId),
        LazyThreadSafetyMode.ExecutionAndPublication);

    private readonly string _dataDirectory;
    private readonly object _metadataGate = new();

    public static WslInstanceStore Default => DefaultStore.Value;

    public WslInstanceStore(string dataDirectory, string sessionId)
    {
        _dataDirectory = dataDirectory;
        SessionId = sessionId;
    }

    public string SessionId { get; }

    public string InstancesDirectory => Path.Combine(_dataDirectory, "instances");

    public string GetInstanceDirectory(string instanceName)
    {
        var sanitized = Sanitize(instanceName);
        if (sanitized.Length == 0)
        {
            throw new WslException($"Instance name '{instanceName}' is not a valid directory name.");
        }

        var instancesDirectory = Path.GetFullPath(InstancesDirectory);
        var directory = Path.GetFullPath(Path.Combine(instancesDirectory, sanitized));
        var prefix = Path.TrimEndingDirectorySeparator(instancesDirectory) + Path.DirectorySeparatorChar;
        if (!directory.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new WslException(
                $"Instance name '{instanceName}' resolves outside the instances directory '{instancesDirectory}'.");
        }

        return directory;
    }

    /// <summary>
    /// Session VM storage. The runtime creates its session VHD (<c>storage.vhdx</c>) and its
    /// named volumes inside this directory; metadata lives in the parent instance directory.
    /// </summary>
    public string GetSessionStorageDirectory(string instanceName) =>
        Path.Combine(GetInstanceDirectory(instanceName), "storage");

    public void WriteMetadata(WslInstanceMetadata metadata)
    {
        var directory = GetInstanceDirectory(metadata.InstanceId);
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, MetadataFileName);
        var json = JsonSerializer.Serialize(metadata, JsonOptions);

        lock (_metadataGate)
        {
            // Write-then-rename so a crash mid-write cannot leave truncated JSON behind.
            // A unique temp name keeps concurrent writers (state transitions racing cleanup)
            // from clobbering each other's temp file.
            var tempPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.WriteAllText(tempPath, json);
                File.Move(tempPath, path, overwrite: true);
            }
            finally
            {
                try
                {
                    File.Delete(tempPath);
                }
                catch
                {
                    // Best effort: a failed write must not leave temp litter behind.
                }
            }
        }
    }

    public WslInstanceMetadata? TryReadMetadata(string instanceName)
    {
        try
        {
            var path = Path.Combine(GetInstanceDirectory(instanceName), MetadataFileName);
            string json;
            lock (_metadataGate)
            {
                if (!File.Exists(path))
                {
                    return null;
                }

                json = File.ReadAllText(path);
            }

            return JsonSerializer.Deserialize<WslInstanceMetadata>(json, JsonOptions);
        }
        catch
        {
            return null;
        }
    }

    public void DeleteInstanceDirectory(string instanceName) =>
        BestEffortDeleteDirectory(GetInstanceDirectory(instanceName));

    internal static string Sanitize(string value)
    {
        var characters = new char[value.Length];
        for (var i = 0; i < value.Length; i++)
        {
            var c = value[i];
            characters[i] = char.IsLetterOrDigit(c) || c is '-' or '_' or '.' ? c : '_';
        }

        // Windows strips trailing dots and spaces from path components, so "a." would alias
        // "a"; map them to '_' so distinct names stay distinct instead of collapsing.
        for (var i = characters.Length - 1; i >= 0 && (characters[i] == '.' || characters[i] == ' '); i--)
        {
            characters[i] = '_';
        }

        return new string(characters);
    }

    internal static void BestEffortDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
            {
                // Never recurse through a junction/reparse point: deleting the link must not
                // touch whatever directory it points at.
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                {
                    Directory.Delete(path);
                    return;
                }

                Directory.Delete(path, recursive: true);
            }
        }
        catch
        {
        }
    }
}
