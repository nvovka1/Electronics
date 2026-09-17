using Initiator.Domain.States;

namespace Initiator.Domain.Devices;

/// <summary>
/// One node in the field. A device enrols itself the first time it checks in:
/// the serial travels in every report and is the key into this record, so there
/// is no list of serial numbers to type in by hand.
/// </summary>
public sealed class Device
{
    /// <summary>Serial from the node's own config, e.g. <c>IN-A41C</c>.</summary>
    public required string Serial { get; set; }

    /// <summary>
    /// Protocol address, the <c>dst</c> the controller puts in a command frame.
    /// Distinct from the serial: this one is a setting, and two nodes sharing it
    /// is a wiring mistake that the fleet list is meant to make visible.
    /// </summary>
    public int NodeId { get; set; }

    public string HardwareId { get; set; } = string.Empty;

    public string FirmwareVersion { get; set; } = string.Empty;

    /// <summary>Short git hash. Semver says what is compatible; the hash says which code this actually is.</summary>
    public string FirmwareHash { get; set; } = string.Empty;

    /// <summary>Built from uncommitted changes, so there is no commit to reproduce it from.</summary>
    public bool FirmwareIsDirty { get; set; }

    public string BuildType { get; set; } = string.Empty;

    public int ProtocolVersion { get; set; }

    public DateTime EnrolledAt { get; set; }

    public DateTime LastSeenAt { get; set; }

    public DeviceHealth? LastHealth { get; set; }

    // --- state ------------------------------------------------------------

    /// <summary>
    /// The last state this node reported. It is a cache of something that lives
    /// on the device: the node can move — by an auto-arm, or because the handheld
    /// controller commanded it — before this service hears about it.
    /// </summary>
    public NodeState CurrentState { get; set; } = NodeStateMachine.BootState;

    /// <summary>When this service learned of the current state. Absolute time, unlike the node's own clock.</summary>
    public DateTime StateChangedAt { get; set; }

    /// <summary>What caused the node to enter its current state.</summary>
    public CommandSource StateSource { get; set; }

    /// <summary>
    /// The node's boot count when it reported <see cref="CurrentState"/>, and
    /// its monotonic clock at that moment. Together they order two reports from
    /// the same node.
    /// </summary>
    /// <remarks>
    /// State arrives on two paths — health reports and state events — and HTTP
    /// does not promise to deliver them in the order they were sent. Without an
    /// ordering the newest write wins, so a health report that was in flight
    /// while the node armed itself would put the dashboard back to INIT and
    /// leave it there. Comparing receipt time cannot fix it, because receipt
    /// time is exactly what is out of order.
    ///
    /// The pair is compared lexicographically: a higher boot count always wins,
    /// because the node's clock restarts at zero on every boot and a raw
    /// millisecond comparison would treat a fresh reboot as ancient history.
    /// </remarks>
    public int StateBootCount { get; set; }

    public long StateTimestampMs { get; set; }

    /// <summary>
    /// The node's configured auto-arm timeout, as it reported it. Stored per
    /// device because it is settable per node over the serial shell, so a
    /// hard-coded five minutes here would be wrong for any node that was tuned.
    /// </summary>
    public int AutoArmTimeoutSeconds { get; set; }

    /// <summary>Free text from the operator: where it is, which shot, who to call.</summary>
    public string? Notes { get; set; }

    /// <summary>Retired on purpose, so it stops being reported as missing.</summary>
    public bool IsDecommissioned { get; set; }

    /// <summary>
    /// Roughly when this node will arm itself, or null when no countdown is
    /// running. <b>An estimate, not a clock the node is obeying</b> — it is this
    /// service's arithmetic over the last report it received. The node's own
    /// countdown is the only real one, and the auto-arm state event is what
    /// settles it.
    /// </summary>
    public DateTime? AutoArmDueAt =>
        NodeStateMachine.StartsCountdown(CurrentState) && AutoArmTimeoutSeconds > 0
            ? StateChangedAt.AddSeconds(AutoArmTimeoutSeconds)
            : null;
}
