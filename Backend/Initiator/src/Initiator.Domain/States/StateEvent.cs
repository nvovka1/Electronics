namespace Initiator.Domain.States;

/// <summary>
/// One transition, as the node reported it. Rejections are recorded too: "the
/// controller tried to FIRE a node that was still in INIT" is exactly the kind
/// of thing you want in the record afterwards, and it is invisible if only
/// successes are stored.
/// </summary>
public sealed class StateEvent
{
    public string Id { get; set; } = string.Empty;

    public required string DeviceSerial { get; set; }

    /// <summary>
    /// The node's own monotonic clock — milliseconds since it booted. Nodes have
    /// no RTC, so this is never wall-clock time and never comparable across a
    /// reboot. <see cref="ReceivedAt"/> is the only absolute time here.
    /// </summary>
    public long TimestampMs { get; set; }

    /// <summary>
    /// The node's boot count when this happened. Stored because
    /// <see cref="TimestampMs"/> restarts at zero on every boot, so it cannot
    /// order two events on its own — and because "which run of the firmware was
    /// this" is worth knowing when reading a history afterwards.
    /// </summary>
    public int BootCount { get; set; }

    public DateTime ReceivedAt { get; set; }

    public NodeState FromState { get; set; }

    public NodeState ToState { get; set; }

    /// <summary>
    /// Null for an auto-arm: no command caused it. Any non-null value has a
    /// <see cref="Source"/> of <see cref="CommandSource.Lora"/> or
    /// <see cref="CommandSource.Api"/>.
    /// </summary>
    public CommandType? Command { get; set; }

    public CommandSource Source { get; set; }

    public bool Accepted { get; set; }

    public RejectReason Reason { get; set; }

    /// <summary>Set when this event resolved a command queued on the dashboard.</summary>
    public string? CommandId { get; set; }

    public bool StateChanged => FromState != ToState;

    /// <summary>
    /// The node armed itself. Worth singling out in the UI: it is the one
    /// transition with nobody behind it.
    /// </summary>
    public bool IsAutoArm =>
        Source == CommandSource.Timer &&
        FromState == NodeState.Init &&
        ToState == NodeState.Armed;
}
