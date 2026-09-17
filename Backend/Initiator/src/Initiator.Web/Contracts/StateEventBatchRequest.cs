namespace Initiator.Web.Contracts;

/// <summary>
/// Transitions a node has made since it last managed to reach this service.
/// </summary>
/// <remarks>
/// A batch rather than one call per transition, because the node buffers these
/// while it is offline and flushes them when the connection comes back. Offline
/// is not an error for these devices, so the normal case includes a node
/// arriving with a backlog.
/// </remarks>
public sealed class StateEventBatchRequest
{
    public List<StateEventRequest> Events { get; set; } = [];
}

public sealed class StateEventRequest
{
    /// <summary>The node's own monotonic clock — milliseconds since it booted.</summary>
    public long TimestampMs { get; set; }

    /// <summary>
    /// The node's boot count when this happened. Ordering needs it: the clock
    /// above restarts at zero on every boot.
    /// </summary>
    public int BootCount { get; set; }

    /// <summary>A <c>NodeState</c> value.</summary>
    public byte FromState { get; set; }

    public byte ToState { get; set; }

    /// <summary>
    /// A <c>CommandType</c> value, or null for the auto-arm — the one transition
    /// with no command behind it.
    /// </summary>
    public byte? Command { get; set; }

    /// <summary>0 = lora, 1 = api, 2 = timer. Matches <c>CommandSource</c>.</summary>
    public byte Source { get; set; }

    public bool Accepted { get; set; }

    /// <summary>A <c>RejectReason</c> value. Zero when accepted.</summary>
    public byte Reason { get; set; }

    /// <summary>
    /// Set when this transition was the node acting on a command it collected
    /// from the queue, so the command can be resolved by the same post.
    /// </summary>
    public string? CommandId { get; set; }
}
