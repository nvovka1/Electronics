namespace Initiator.Web.Contracts;

/// <summary>
/// A command for the node to act on. The endpoint answers 204 when there is
/// nothing queued, which is the common case and the cheapest one for a device
/// on a battery to handle.
/// </summary>
public sealed class NextCommandResponse
{
    /// <summary>Quote this back when reporting what happened, so the queue entry can be closed.</summary>
    public required string CommandId { get; init; }

    /// <summary>A <c>CommandType</c> value.</summary>
    public required byte Command { get; init; }

    public required DateTime QueuedAt { get; init; }
}

/// <summary>What a node did with a command it collected.</summary>
public sealed class CommandResultRequest
{
    public bool Accepted { get; set; }

    /// <summary>A <c>RejectReason</c> value. Zero when accepted.</summary>
    public byte Reason { get; set; }

    /// <summary>The state the node was in afterwards, as a <c>NodeState</c> value.</summary>
    public byte State { get; set; }

    public long TimestampMs { get; set; }

    public int BootCount { get; set; }
}
