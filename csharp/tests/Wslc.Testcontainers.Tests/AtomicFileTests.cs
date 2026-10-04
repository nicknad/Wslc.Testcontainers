using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class AtomicFileTests
{
    [Fact]
    public void Commit_replaces_the_existing_destination()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-atomic");
        try
        {
            var destination = Path.Combine(directory.FullName, "dest.txt");
            File.WriteAllText(destination, "old");
            var temp = AtomicFile.CreateTempPath(destination);
            File.WriteAllText(temp, "new");

            AtomicFile.Commit(temp, destination);

            Assert.Equal("new", File.ReadAllText(destination));
            Assert.False(File.Exists(temp));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void Discard_leaves_the_existing_destination_untouched()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-atomic");
        try
        {
            var destination = Path.Combine(directory.FullName, "dest.txt");
            File.WriteAllText(destination, "old");
            var temp = AtomicFile.CreateTempPath(destination);
            File.WriteAllText(temp, "partial");

            AtomicFile.Discard(temp);

            Assert.False(File.Exists(temp));
            Assert.Equal("old", File.ReadAllText(destination));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void EnsureReplaceable_rejects_directories()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-atomic");
        try
        {
            Assert.Throws<WslProcessException>(() => AtomicFile.EnsureReplaceable(directory.FullName));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void EnsureReplaceable_allows_missing_and_regular_files()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-atomic");
        try
        {
            AtomicFile.EnsureReplaceable(Path.Combine(directory.FullName, "missing.txt"));
            var regular = Path.Combine(directory.FullName, "regular.txt");
            File.WriteAllText(regular, "data");
            AtomicFile.EnsureReplaceable(regular);
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void EnsureReplaceable_rejects_reparse_points()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-atomic");
        try
        {
            var target = Path.Combine(directory.FullName, "target.txt");
            File.WriteAllText(target, "payload");
            var link = Path.Combine(directory.FullName, "link.txt");
            try
            {
                File.CreateSymbolicLink(link, target);
            }
            catch (Exception exception) when (exception is UnauthorizedAccessException or IOException or PlatformNotSupportedException)
            {
                Assert.Skip("Symbolic link creation is not available in this environment.");
            }

            Assert.Throws<WslProcessException>(() => AtomicFile.EnsureReplaceable(link));
            Assert.Equal("payload", File.ReadAllText(target));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }
}
