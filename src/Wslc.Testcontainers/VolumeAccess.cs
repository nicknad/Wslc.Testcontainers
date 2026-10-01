namespace Wslc.Testcontainers;

/// <summary>Access mode for a Windows directory mounted into the container.</summary>
public enum VolumeAccess
{
    /// <summary>Read-write mount (default).</summary>
    ReadWrite = 0,

    /// <summary>Read-only mount.</summary>
    ReadOnly = 1,
}
