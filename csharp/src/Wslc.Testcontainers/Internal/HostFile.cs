using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Opens a host file for reading without following reparse points. Validation and the copy
/// read from the same handle, so a path swapped to a symlink or junction after the builder
/// checks cannot redirect the transfer.
/// </summary>
internal static class HostFile
{
    /// <summary>Maximum number of bytes a single host file copy may transfer.</summary>
    public const long MaxCopyBytes = 1024L * 1024L * 1024L;

    private const uint GenericRead = 0x80000000;
    private const uint FileShareRead = 0x00000001;
    private const uint OpenExisting = 3;
    private const uint FileFlagOverlapped = 0x40000000;
    private const uint FileFlagSequentialScan = 0x08000000;
    private const uint FileFlagOpenReparsePoint = 0x00200000;
    private const uint FileFlagBackupSemantics = 0x02000000;
    private const int ErrorFileNotFound = 2;
    private const int ErrorPathNotFound = 3;

    /// <summary>
    /// Opens <paramref name="path"/> read-only, refusing to follow reparse points and rejecting
    /// directories and files above <see cref="MaxCopyBytes"/>. Disposing the returned stream
    /// closes the handle.
    /// </summary>
    public static FileStream OpenRead(string path)
    {
        // FILE_FLAG_BACKUP_SEMANTICS is what lets a directory (or a junction, which is a
        // directory reparse point) be opened at all, so it reaches the attribute checks below
        // instead of failing with ERROR_ACCESS_DENIED. FILE_FLAG_OVERLAPPED pairs with the
        // asynchronous FileStream returned below.
        var handle = CreateFile(
            Path.GetFullPath(path),
            GenericRead,
            FileShareRead,
            IntPtr.Zero,
            OpenExisting,
            FileFlagOpenReparsePoint | FileFlagSequentialScan | FileFlagBackupSemantics | FileFlagOverlapped,
            IntPtr.Zero);

        if (handle.IsInvalid)
        {
            var error = Marshal.GetLastWin32Error();
            handle.Dispose();
            if (error is ErrorFileNotFound or ErrorPathNotFound)
            {
                throw new WslProcessException($"Host file '{path}' does not exist.");
            }

            throw new WslProcessException($"Failed to open Host file '{path}': Win32 error {error}.");
        }

        try
        {
            var attributes = File.GetAttributes(handle);
            if ((attributes & FileAttributes.ReparsePoint) != 0)
            {
                throw new WslProcessException(
                    $"Host file '{path}' is a reparse point (symlink or junction); refusing to follow it.");
            }

            if ((attributes & FileAttributes.Directory) != 0)
            {
                throw new WslProcessException($"Host path '{path}' is a directory; only files can be copied.");
            }

            var length = RandomAccess.GetLength(handle);
            if (length > MaxCopyBytes)
            {
                throw new WslProcessException($"Copying '{path}' exceeds 1 GiB limit ({length} bytes).");
            }

            return new FileStream(handle, FileAccess.Read, 64 * 1024, isAsync: true);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    // DllImport because LibraryImport's generated marshalling requires AllowUnsafeBlocks.
    [DllImport("kernel32.dll", EntryPoint = "CreateFileW", SetLastError = true, CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern SafeFileHandle CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);
}
