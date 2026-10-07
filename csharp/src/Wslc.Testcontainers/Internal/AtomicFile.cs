namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Writes a host-file copy to a sibling temp file and swaps it into place only after the
/// container transfer succeeded, so a failed copy never truncates the existing destination.
/// </summary>
internal static class AtomicFile
{
    /// <summary>
    /// Rejects destinations that would redirect the write: directories and reparse points
    /// (symlinks/junctions). A swapped-in link is never followed because the replacement
    /// happens with an atomic move onto the link entry itself.
    /// </summary>
    public static void EnsureReplaceable(string destinationPath)
    {
        if (Directory.Exists(destinationPath))
        {
            throw new WslProcessException($"Refusing to write '{destinationPath}': the destination is a directory.");
        }

        if (File.Exists(destinationPath) && (File.GetAttributes(destinationPath) & FileAttributes.ReparsePoint) != 0)
        {
            throw new WslProcessException(
                $"Refusing to write '{destinationPath}': the destination is a reparse point (symlink or junction).");
        }
    }

    /// <summary>Returns a fresh temp path in the destination directory.</summary>
    public static string CreateTempPath(string destinationPath)
    {
        var directory = Path.GetDirectoryName(destinationPath) ?? ".";
        return Path.Combine(directory, $".{Path.GetFileName(destinationPath)}.{Guid.NewGuid():N}.wslc-tmp");
    }

    /// <summary>Atomically replaces the destination with the finished temp file.</summary>
    public static void Commit(string tempPath, string destinationPath) =>
        File.Move(tempPath, destinationPath, overwrite: true);

    /// <summary>
    /// Writes text through the same temp-plus-rename sequence as a copy commit, so a crash
    /// mid-write cannot leave truncated content behind. The per-call temp name (the destination
    /// file name plus a GUID) keeps concurrent writers from clobbering each other's temp file;
    /// it is removed whether or not the rename succeeded.
    /// </summary>
    public static void WriteAllText(string path, string content)
    {
        var tempPath = CreateTempPath(path);
        try
        {
            File.WriteAllText(tempPath, content);
            Commit(tempPath, path);
        }
        finally
        {
            Discard(tempPath);
        }
    }

    /// <summary>Best-effort removal of an abandoned temp file.</summary>
    public static void Discard(string tempPath)
    {
        try
        {
            File.Delete(tempPath);
        }
        catch
        {
        }
    }
}
