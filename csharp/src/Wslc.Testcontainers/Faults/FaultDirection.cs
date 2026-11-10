namespace Wslc.Testcontainers.Faults;

/// <summary>Data-flow direction a fault applies to.</summary>
public enum FaultDirection
{
    /// <summary>From the test client through the relay to the target (broker).</summary>
    ClientToBroker,

    /// <summary>From the target (broker) through the relay back to the test client.</summary>
    BrokerToClient,

    /// <summary>Both directions.</summary>
    Both,
}
