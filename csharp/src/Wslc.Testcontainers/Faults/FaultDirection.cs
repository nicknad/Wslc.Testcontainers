namespace Wslc.Testcontainers.Faults;

/// <summary>Data-flow direction a fault applies to.</summary>
public enum FaultDirection
{
    /// <summary>From the test client through the relay to the target.</summary>
    ClientToTarget,

    /// <summary>From the target through the relay back to the test client.</summary>
    TargetToClient,

    /// <summary>Both directions.</summary>
    Both,
}
