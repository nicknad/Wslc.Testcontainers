namespace Wslc.Testcontainers;

/// <summary>VHD allocation strategy for a session volume.</summary>
public enum VhdAllocationType
{
    /// <summary>Grows on demand (default).</summary>
    Dynamic = 0,

    /// <summary>Pre-allocates the full size.</summary>
    Fixed = 1,
}
