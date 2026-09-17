using Initiator.Domain.States;

namespace Initiator.Domain.Devices;

/// <summary>
/// Turns "when did it last speak, and what did it say" into the one thing an
/// operator needs from a list of forty rows: which of these needs me.
/// </summary>
/// <remarks>
/// A pure function of the device and the current time, so every boundary is a
/// host test and none of it needs a database.
/// </remarks>
public static class DeviceStatusPolicy
{
    /// <summary>
    /// A node that has missed a couple of scheduled check-ins is late, not lost:
    /// one dropped request is normal over WiFi on a battery.
    /// </summary>
    public const int LateAfterMissedReports = 2;

    /// <summary>Beyond this, somebody has to go and look.</summary>
    public static readonly TimeSpan MissingAfter = TimeSpan.FromMinutes(30);

    public static DeviceStatus Evaluate(Device device, TimeSpan reportPeriod, DateTime utcNow)
    {
        ArgumentNullException.ThrowIfNull(device);

        if (device.IsDecommissioned) return DeviceStatus.Decommissioned;

        var silence = utcNow - device.LastSeenAt;

        // Silence outranks everything else. A node that reported a clean
        // self-test and then stopped talking is not healthy, whatever its last
        // report said.
        if (silence >= MissingAfter) return DeviceStatus.Missing;

        var lateAfter = reportPeriod * LateAfterMissedReports;
        if (reportPeriod > TimeSpan.Zero && silence > lateAfter) return DeviceStatus.Late;

        var health = device.LastHealth;
        if (health is null) return DeviceStatus.Degraded;

        if (!health.PostPassed || !health.BatteryIsTrusted) return DeviceStatus.Degraded;

        return DeviceStatus.Healthy;
    }

    /// <summary>Whether this status should draw an eye in a long list.</summary>
    public static bool NeedsAttention(DeviceStatus status) =>
        status is DeviceStatus.Degraded or DeviceStatus.Late or DeviceStatus.Missing;

    /// <summary>
    /// Whether this node should be prominent because of what it is doing rather
    /// than how it is: anything not in SAFE is a node somebody should know about,
    /// and a node that is silent while not in SAFE is the case that matters most.
    /// </summary>
    /// <remarks>
    /// This is separate from <see cref="NeedsAttention"/> on purpose. A healthy
    /// node sitting in ARMED is not a fault — nothing is wrong with it — but it
    /// is not something to leave at the bottom of a list either.
    /// </remarks>
    public static bool IsLive(Device device)
    {
        ArgumentNullException.ThrowIfNull(device);

        return device is { IsDecommissioned: false, CurrentState: not NodeState.Safe };
    }
}
