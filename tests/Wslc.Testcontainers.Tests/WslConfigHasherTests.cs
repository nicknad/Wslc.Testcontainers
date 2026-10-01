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
            Ports = new[] { 8080, 5432 },
        };
        var second = first with
        {
            Environment = new Dictionary<string, string> { ["B"] = "2", ["A"] = "1" },
            Ports = new[] { 5432, 8080 },
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
}
