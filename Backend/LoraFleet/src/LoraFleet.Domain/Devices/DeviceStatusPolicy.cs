namespace LoraFleet.Domain.Devices;

/// <summary>
/// Turns "when did it last speak, and what did it say" into the one thing an
/// operator actually needs: is this worth driving out for.
/// </summary>
/// <remarks>
/// A pure function of the device and the current time, so every boundary is
/// covered by host tests and none of it needs a database.
/// </remarks>
public static class DeviceStatusPolicy
{
    /// <summary>
    /// A node that has missed a couple of scheduled check-ins is late, not
    /// lost: one dropped uplink is normal on any radio link.
    /// </summary>
    public const int LateAfterMissedReports = 2;

    /// <summary>
    /// Beyond this a node is treated as missing. Two hours is the threshold the
    /// troubleshooting guide uses to decide between waiting and a site visit.
    /// </summary>
    public static readonly TimeSpan MissingAfter = TimeSpan.FromHours(2);

    public static DeviceStatus Evaluate(Device device, TimeSpan reportPeriod, DateTime utcNow)
    {
        ArgumentNullException.ThrowIfNull(device);

        if (device.IsDecommissioned) return DeviceStatus.Decommissioned;

        var silence = utcNow - device.LastSeenAt;

        // Silence outranks everything else. A node that reported a clean POST
        // and then stopped talking is not healthy, whatever its last frame said.
        if (silence >= MissingAfter) return DeviceStatus.Missing;

        var lateAfter = reportPeriod * LateAfterMissedReports;
        if (reportPeriod > TimeSpan.Zero && silence > lateAfter) return DeviceStatus.Late;

        var health = device.LastHealth;
        if (health is null) return DeviceStatus.Degraded;

        // An untrusted battery counts as degraded: the node is telling us it
        // cannot measure its own supply, which also disables its write gate.
        if (!health.PostPassed || !health.BatteryIsTrusted) return DeviceStatus.Degraded;

        return DeviceStatus.Healthy;
    }

    /// <summary>Whether this status should draw an operator's eye in a list of forty.</summary>
    public static bool NeedsAttention(DeviceStatus status) =>
        status is DeviceStatus.Degraded or DeviceStatus.Late or DeviceStatus.Missing;
}
