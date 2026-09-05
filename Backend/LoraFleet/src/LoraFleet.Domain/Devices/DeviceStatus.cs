namespace LoraFleet.Domain.Devices;

/// <summary>
/// What an operator needs to decide from the list: is anything wrong, and is it
/// worth driving out for.
/// </summary>
public enum DeviceStatus
{
    /// <summary>Checked in recently, POST clean.</summary>
    Healthy,

    /// <summary>Checked in recently, but reporting a failed POST block or an untrusted battery.</summary>
    Degraded,

    /// <summary>Overdue for a check-in, but not long enough to be worth a trip.</summary>
    Late,

    /// <summary>Silent long enough that somebody has to go and look.</summary>
    Missing,

    /// <summary>Retired on purpose. Not counted as missing.</summary>
    Decommissioned,
}
