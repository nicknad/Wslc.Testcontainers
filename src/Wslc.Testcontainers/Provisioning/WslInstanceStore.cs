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

    public static WslInstanceStore Default { get; } = new(WslcEnvironment.DataDirectory, WslcEnvironment.SessionId);

    public WslInstanceStore(string dataDirectory, string sessionId)
    {
        DataDirectory = dataDirectory;
        SessionId = sessionId;
    }

    public string DataDirectory { get; }

    public string SessionId { get; }

    public string InstancesDirectory => Path.Combine(DataDirectory, "instances");

    public string GetInstanceDirectory(string instanceName) =>
        Path.Combine(InstancesDirectory, Sanitize(instanceName));

    /// <summary>
    /// Session VM storage. The WSL container runtime requires this directory to be empty when
    /// the session starts, so metadata lives in the parent instance directory.
    /// </summary>
    public string GetSessionStorageDirectory(string instanceName) =>
        Path.Combine(GetInstanceDirectory(instanceName), "storage");

    public void WriteMetadata(WslInstanceMetadata metadata)
    {
        var directory = GetInstanceDirectory(metadata.InstanceId);
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, MetadataFileName);
        var tempPath = path + ".tmp";

        // Write-then-rename so a crash mid-write cannot leave truncated JSON behind.
        // Corrupt metadata is treated as "not ours" by the reaper and would leak storage.
        File.WriteAllText(tempPath, JsonSerializer.Serialize(metadata, JsonOptions));
        File.Move(tempPath, path, overwrite: true);
    }

    public WslInstanceMetadata? TryReadMetadata(string instanceName)
    {
        try
        {
            var path = Path.Combine(GetInstanceDirectory(instanceName), MetadataFileName);
            if (!File.Exists(path))
            {
                return null;
            }

            return JsonSerializer.Deserialize<WslInstanceMetadata>(File.ReadAllText(path), JsonOptions);
        }
        catch
        {
            return null;
        }
    }

    public void DeleteInstanceDirectory(string instanceName) =>
        BestEffortDeleteDirectory(GetInstanceDirectory(instanceName));

    internal static string Sanitize(string value) =>
        string.Create(value.Length, value, static (span, source) =>
        {
            for (var i = 0; i < source.Length; i++)
            {
                var c = source[i];
                span[i] = char.IsLetterOrDigit(c) || c is '-' or '_' or '.' ? c : '_';
            }
        });

    internal static void BestEffortDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path, recursive: true);
            }
        }
        catch
        {
        }
    }
}
