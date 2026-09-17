namespace Initiator.Domain.Devices;

/// <summary>Whether anything about this node needs a human.</summary>
public enum DeviceStatus
{
    /// <summary>Checked in recently, self-test clean, battery readable.</summary>
    Healthy,

    /// <summary>Checked in recently, but reporting a failed self-test block or an untrusted battery.</summary>
    Degraded,

    /// <summary>Overdue for a check-in, but not long enough to be worth a trip.</summary>
    Late,

    /// <summary>Silent long enough that somebody has to go and look.</summary>
    Missing,

    /// <summary>Retired on purpose. Not counted as missing.</summary>
    Decommissioned,
}
