using Initiator.Domain.States;

namespace Initiator.Domain.Commands;

/// <summary>
/// A command queued on the dashboard for a node to collect on its next poll.
/// </summary>
/// <remarks>
/// Polling means there is up to one poll interval between clicking and the node
/// hearing about it. That is the accepted trade for a node that never needs an
/// inbound connection — the controller is the real-time path, and this one is
/// for setup and for the record.
/// </remarks>
public sealed class Command
{
    public string Id { get; set; } = string.Empty;

    public required string DeviceSerial { get; set; }

    public CommandType Type { get; set; }

    public CommandStatus Status { get; set; }

    public DateTime QueuedAt { get; set; }

    public DateTime? DeliveredAt { get; set; }

    /// <summary>When it reached a terminal status, whichever one.</summary>
    public DateTime? ResolvedAt { get; set; }

    /// <summary>
    /// The state the dashboard believed the node was in when this was queued.
    /// Kept so that a later rejection can be read honestly: the interesting
    /// question is always whether the view was stale.
    /// </summary>
    public NodeState QueuedAgainstState { get; set; }

    public RejectReason Reason { get; set; }

    /// <summary>Free text from whoever queued it.</summary>
    public string? Note { get; set; }

    public bool IsTerminal => Status is
        CommandStatus.Applied or
        CommandStatus.Rejected or
        CommandStatus.Expired or
        CommandStatus.Cancelled;

    /// <summary>Still collectable by a node.</summary>
    public bool IsOutstanding => Status == CommandStatus.Pending;
}
