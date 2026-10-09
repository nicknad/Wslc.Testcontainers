using System.Text.Json;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests.Provisioning;

public sealed class WslConfigHasherTests
{
    [Fact]
    public void Identical_configurations_produce_identical_hashes()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine:latest",
            Command = "sleep",
            CommandArguments = new[] { "infinity" },
            Environment = new Dictionary<string, string> { ["A"] = "1", ["B"] = "2" },
            PortMappings = new[] { new WslPortMapping(8080, null), new WslPortMapping(5432, null) },
        };
        var second = first with
        {
            Environment = new Dictionary<string, string> { ["B"] = "2", ["A"] = "1" },
            PortMappings = new[] { new WslPortMapping(5432, null), new WslPortMapping(8080, null) },
        };

        Assert.Equal(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void Environment_values_change_the_hash()
    {
        var first = new WslContainerConfiguration { Image = "alpine:latest" };
        var second = first with { Environment = new Dictionary<string, string> { ["A"] = "1" } };
        var third = second with { Environment = new Dictionary<string, string> { ["A"] = "2" } };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
        Assert.NotEqual(WslConfigHasher.Compute(second), WslConfigHasher.Compute(third));
    }

    [Fact]
    public void Command_argument_order_is_significant()
    {
        var first = new WslContainerConfiguration { Image = "alpine", Command = "sh", CommandArguments = new[] { "-c", "one" } };
        var second = first with { CommandArguments = new[] { "one", "-c" } };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void Bind_address_changes_the_hash()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            PortMappings = new[] { new WslPortMapping(8080, null) },
        };
        var bound = first with
        {
            PortMappings = new[] { new WslPortMapping(8080, "127.0.0.1") },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(bound));
    }

    [Fact]
    public void Resource_networking_and_volume_settings_change_the_hash()
    {
        var first = new WslContainerConfiguration { Image = "alpine" };
        var cpu = first with { CpuCount = 2u };
        var memory = first with { MemorySizeInMB = 2048u };
        var netmode = first with { NetworkingMode = ContainerNetworkMode.Isolated };
        var named = first with
        {
            ScratchVolumes = new[] { new WslScratchVolume("data", "/data", ReadOnly: false, SizeBytes: 100, Type: VhdAllocationType.Dynamic) },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(cpu));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(memory));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(netmode));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(named));
    }

    [Fact]
    public void Scratch_volume_order_does_not_change_the_hash()
    {
        var volumes = new[]
        {
            new WslScratchVolume("a", "/a", ReadOnly: false, SizeBytes: 100, Type: VhdAllocationType.Dynamic),
            new WslScratchVolume("b", "/b", ReadOnly: false, SizeBytes: 200, Type: VhdAllocationType.Dynamic),
        };
        var first = new WslContainerConfiguration { Image = "alpine", ScratchVolumes = volumes };
        var reordered = first with { ScratchVolumes = new[] { volumes[1], volumes[0] } };

        Assert.Equal(WslConfigHasher.Compute(first), WslConfigHasher.Compute(reordered));
    }

    [Fact]
    public void Volume_read_only_flag_changes_the_hash()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            Volumes = new[] { new WslVolumeMount(@"C:\data", "/data", ReadOnly: false) },
        };
        var second = first with
        {
            Volumes = new[] { new WslVolumeMount(@"C:\data", "/data", ReadOnly: true) },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void Delimiter_characters_in_environment_values_cannot_collide()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            Environment = new Dictionary<string, string> { ["A"] = "x\nenv:B=y" },
        };
        var second = new WslContainerConfiguration
        {
            Image = "alpine",
            Environment = new Dictionary<string, string> { ["A"] = "x", ["B"] = "y" },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void Argument_separator_characters_cannot_collide()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            Command = "sh",
            CommandArguments = new[] { "a\u001fb" },
        };
        var second = first with { CommandArguments = new[] { "a", "b" } };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void Null_and_empty_values_hash_differently()
    {
        var first = new WslContainerConfiguration { Image = "alpine", WorkingDirectory = null };
        var second = first with { WorkingDirectory = string.Empty };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(second));
    }

    [Fact]
    public void File_paths_and_destinations_change_the_hash()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            Files = new[] { new WslFileCopy(@"C:\one.txt", "/tmp/file.txt") },
        };
        var otherSource = first with { Files = new[] { new WslFileCopy(@"C:\two.txt", "/tmp/file.txt") } };
        var otherDestination = first with { Files = new[] { new WslFileCopy(@"C:\one.txt", "/tmp/other.txt") } };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(otherSource));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(otherDestination));
    }

    [Fact]
    public void File_content_does_not_change_the_hash()
    {
        // WithFile sources are copied into the container on every start, so identity tracks the
        // configured paths only; hashing contents would put file I/O behind the Name property.
        var path = Path.Combine(Path.GetTempPath(), $"wslc-hash-{Guid.NewGuid():N}.txt");
        try
        {
            File.WriteAllText(path, "one");
            var configuration = new WslContainerConfiguration
            {
                Image = "alpine",
                Files = new[] { new WslFileCopy(path, "/tmp/file.txt") },
            };
            var before = WslConfigHasher.Compute(configuration);

            File.WriteAllText(path, "two");
            var after = WslConfigHasher.Compute(configuration);

            Assert.Equal(before, after);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Shared_golden_hash_vectors_match()
    {
        var vectors = LoadGoldenVectors();

        Assert.NotEmpty(vectors);
        foreach (var vector in vectors)
        {
            var actual = WslConfigHasher.Compute(vector.Configuration);
            Assert.True(
                string.Equals(actual, vector.Sha256, StringComparison.Ordinal),
                $"Golden vector '{vector.Name}' diverged: expected {vector.Sha256}, got {actual}.");
        }
    }

    [Fact]
    public void Reuse_flag_does_not_change_the_hash()
    {
        // Reuse is a session policy switch, not configuration metadata: the two vectors differ
        // only in their reuse flag and are expected to share one identity.
        var configurations = LoadGoldenVectors().ToDictionary(
            vector => vector.Name,
            vector => vector.Configuration,
            StringComparer.Ordinal);

        Assert.Equal(
            WslConfigHasher.Compute(configurations["reuse-true"]),
            WslConfigHasher.Compute(configurations["reuse-false"]));
    }

    private static List<(string Name, WslContainerConfiguration Configuration, string Sha256)>
        LoadGoldenVectors()
    {
        var fixturePath = Path.Combine(FindRepositoryRoot(), "tests", "fixtures", "config_hash_vectors.json");
        using var document = JsonDocument.Parse(File.ReadAllText(fixturePath));

        var vectors = new List<(string Name, WslContainerConfiguration Configuration, string Sha256)>();
        foreach (var element in document.RootElement.EnumerateArray())
        {
            vectors.Add((
                element.GetProperty("name").GetString()!,
                ToConfiguration(element),
                element.GetProperty("sha256").GetString()!));
        }

        return vectors;
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "AGENTS.md")))
            {
                return directory.FullName;
            }
        }

        throw new InvalidOperationException($"Could not locate the repository root from {AppContext.BaseDirectory}.");
    }

    private static WslContainerConfiguration ToConfiguration(JsonElement element)
    {
        var environment = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var property in element.GetProperty("env").EnumerateObject())
        {
            environment[property.Name] = property.Value.GetString()!;
        }

        return new WslContainerConfiguration
        {
            Image = ReadNullableString(element, "image"),
            Command = ReadNullableString(element, "command"),
            CommandArguments = element.GetProperty("args")
                .EnumerateArray()
                .Select(value => value.GetString()!)
                .ToArray(),
            WorkingDirectory = ReadNullableString(element, "workingDirectory"),
            Environment = environment,
            Files = element.GetProperty("files")
                .EnumerateArray()
                .Select(value => new WslFileCopy(
                    value.GetProperty("host").GetString()!,
                    value.GetProperty("container").GetString()!))
                .ToArray(),
            Volumes = element.GetProperty("volumes")
                .EnumerateArray()
                .Select(value => new WslVolumeMount(
                    value.GetProperty("host").GetString()!,
                    value.GetProperty("container").GetString()!,
                    value.GetProperty("readOnly").GetBoolean()))
                .ToArray(),
            ScratchVolumes = element.GetProperty("sessionVolumes")
                .EnumerateArray()
                .Select(value => new WslScratchVolume(
                    value.GetProperty("name").GetString()!,
                    value.GetProperty("container").GetString()!,
                    value.GetProperty("readOnly").GetBoolean(),
                    value.GetProperty("size").GetUInt64(),
                    value.GetProperty("type").GetString() == "fixed"
                        ? VhdAllocationType.Fixed
                        : VhdAllocationType.Dynamic))
                .ToArray(),
            PortMappings = element.GetProperty("ports")
                .EnumerateArray()
                .Select(value => new WslPortMapping(
                    value.GetProperty("container").GetInt32(),
                    ReadNullableString(value, "bind"),
                    value.TryGetProperty("host", out var host) && host.ValueKind != JsonValueKind.Null
                        ? host.GetInt32()
                        : 0))
                .ToArray(),
            NetworkingMode = ReadNetworkingMode(element),
            CpuCount = ReadNullableUInt32(element, "cpu"),
            MemorySizeInMB = ReadNullableUInt32(element, "memoryMB"),
            Reuse = ReadNullableBool(element, "reuse"),
        };
    }

    private static string? ReadNullableString(JsonElement element, string propertyName) =>
        element.GetProperty(propertyName).ValueKind == JsonValueKind.Null
            ? null
            : element.GetProperty(propertyName).GetString();

    private static uint? ReadNullableUInt32(JsonElement element, string propertyName) =>
        element.GetProperty(propertyName).ValueKind == JsonValueKind.Null
            ? null
            : element.GetProperty(propertyName).GetUInt32();

    private static bool? ReadNullableBool(JsonElement element, string propertyName) =>
        element.GetProperty(propertyName).ValueKind == JsonValueKind.Null
            ? null
            : element.GetProperty(propertyName).GetBoolean();

    private static ContainerNetworkMode? ReadNetworkingMode(JsonElement element) =>
        ReadNullableString(element, "networkingMode") switch
        {
            null => null,
            "bridged" => ContainerNetworkMode.Bridged,
            "none" => ContainerNetworkMode.Isolated,
            var value => throw new InvalidOperationException($"Unknown networkingMode '{value}'."),
        };
}
