using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Provisioning;
using Xunit;

namespace Wslc.Testcontainers.Tests;

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
            PortMappings = new[] { new WslPortMapping(8080, PortProtocol.TCP, null), new WslPortMapping(5432, PortProtocol.TCP, null) },
        };
        var second = first with
        {
            Environment = new Dictionary<string, string> { ["B"] = "2", ["A"] = "1" },
            PortMappings = new[] { new WslPortMapping(5432, PortProtocol.TCP, null), new WslPortMapping(8080, PortProtocol.TCP, null) },
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
    public void Port_protocol_and_bind_address_change_the_hash()
    {
        var first = new WslContainerConfiguration
        {
            Image = "alpine",
            PortMappings = new[] { new WslPortMapping(8080, PortProtocol.TCP, null) },
        };
        var udp = first with
        {
            PortMappings = new[] { new WslPortMapping(8080, PortProtocol.UDP, null) },
        };
        var bound = first with
        {
            PortMappings = new[] { new WslPortMapping(8080, PortProtocol.TCP, "127.0.0.1") },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(udp));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(bound));
    }

    [Fact]
    public void Resource_networking_volume_and_egress_settings_change_the_hash()
    {
        var first = new WslContainerConfiguration { Image = "alpine" };
        var cpu = first with { CpuCount = 2u };
        var memory = first with { MemorySizeInMB = 2048u };
        var netmode = first with { NetworkingMode = ContainerNetworkingMode.None };
        var named = first with
        {
            NamedVolumes = new[] { new WslNamedVolume("data", "/data", ReadOnly: false, SizeBytes: 100, Type: VhdType.Dynamic) },
        };
        var egress = first with
        {
            EgressAllowlist = new EgressAllowlistOptions { AllowedHosts = new[] { "10.0.0.5" } },
        };

        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(cpu));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(memory));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(netmode));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(named));
        Assert.NotEqual(WslConfigHasher.Compute(first), WslConfigHasher.Compute(egress));
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
    public void File_content_changes_the_hash()
    {
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

            Assert.NotEqual(before, after);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Missing_file_and_empty_file_hash_differently()
    {
        var path = Path.Combine(Path.GetTempPath(), $"wslc-hash-{Guid.NewGuid():N}.txt");
        try
        {
            var configuration = new WslContainerConfiguration
            {
                Image = "alpine",
                Files = new[] { new WslFileCopy(path, "/tmp/file.txt") },
            };
            var missing = WslConfigHasher.Compute(configuration);

            File.WriteAllBytes(path, Array.Empty<byte>());
            var empty = WslConfigHasher.Compute(configuration);

            Assert.NotEqual(missing, empty);
        }
        finally
        {
            File.Delete(path);
        }
    }
}
