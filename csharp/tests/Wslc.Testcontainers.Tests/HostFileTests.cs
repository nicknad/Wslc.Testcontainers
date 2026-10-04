using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class HostFileTests
{
    [Fact]
    public async Task OpenRead_returns_content_and_owns_the_handle()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-hostfile");
        try
        {
            var path = Path.Combine(directory.FullName, "payload.txt");
            await File.WriteAllTextAsync(path, "payload", TestContext.Current.CancellationToken);

            var stream = HostFile.OpenRead(path);
            try
            {
                var buffer = new byte[16];
                var read = await stream.ReadAsync(buffer, TestContext.Current.CancellationToken);
                Assert.Equal("payload", Encoding.UTF8.GetString(buffer, 0, read));
            }
            finally
            {
                await stream.DisposeAsync();
            }

            Assert.True(stream.SafeFileHandle.IsClosed);

            // A non-owning SafeFileHandle would also report IsClosed, so prove the OS handle was
            // released by taking an exclusive open.
            using var exclusive = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.None);
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void OpenRead_throws_for_missing_path()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-hostfile");
        try
        {
            var missing = Path.Combine(directory.FullName, "missing.txt");

            Assert.Throws<WslProcessException>(() => HostFile.OpenRead(missing));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void OpenRead_throws_for_directory()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-hostfile");
        try
        {
            var subdirectory = Directory.CreateDirectory(Path.Combine(directory.FullName, "sub")).FullName;

            Assert.Throws<WslProcessException>(() => HostFile.OpenRead(subdirectory));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void OpenRead_throws_for_file_symlink()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-hostfile");
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

            Assert.Throws<WslProcessException>(() => HostFile.OpenRead(link));
            Assert.Equal("payload", File.ReadAllText(target));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [Fact]
    public void OpenRead_throws_for_directory_junction()
    {
        var directory = Directory.CreateTempSubdirectory("wslc-hostfile");
        var junction = Path.Combine(directory.FullName, "junction");
        try
        {
            var target = Directory.CreateDirectory(Path.Combine(directory.FullName, "target")).FullName;
            if (!TryCreateJunction(junction, target))
            {
                Assert.Skip("Junction creation is not available in this environment.");
            }

            Assert.Throws<WslProcessException>(() => HostFile.OpenRead(junction));
            Assert.True(Directory.Exists(target));
        }
        finally
        {
            // Recursive delete fails on a junction whose target is removed first, so unlink it.
            if (Directory.Exists(junction))
            {
                Directory.Delete(junction);
            }

            directory.Delete(recursive: true);
        }
    }

    private static bool TryCreateJunction(string junction, string target)
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo("cmd", $"/c mklink /J \"{junction}\" \"{target}\"")
            {
                CreateNoWindow = true,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            })!;
            process.StandardOutput.ReadToEnd();
            process.StandardError.ReadToEnd();
            process.WaitForExit();
            return process.ExitCode == 0 && Directory.Exists(junction);
        }
        catch (Exception exception) when (exception is Win32Exception or IOException)
        {
            return false;
        }
    }
}
