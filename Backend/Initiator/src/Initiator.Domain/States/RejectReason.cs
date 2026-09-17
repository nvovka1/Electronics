namespace Initiator.Domain.States;

/// <summary>
/// Why a node turned a command down. Travels in the <c>reason</c> byte of
/// <c>MSG_CMD_ACK</c> and in the command result posted back to this service.
/// </summary>
public enum RejectReason : byte
{
    /// <summary>Not a rejection. The command was applied, or was a no-op.</summary>
    Ok = 0,

    /// <summary>The state machine has no edge from the current state for this command.</summary>
    BadTransition = 1,

    /// <summary>Addressed to a different node. Only ever seen on the radio path.</summary>
    BadTarget = 2,

    /// <summary>A counter this node has already seen from that source — a replayed frame.</summary>
    Replay = 3,

    /// <summary>A command byte outside <see cref="CommandType"/>.</summary>
    BadCommand = 4,
}
