namespace Initiator.Domain.States;

/// <summary>
/// What a node is doing right now. The numbers are part of the LoRa protocol
/// and the HTTP API, not an implementation detail: the firmware sends these
/// values in <c>MSG_CMD_ACK</c> and <c>MSG_STATE</c>.
/// </summary>
public enum NodeState : byte
{
    /// <summary>Nothing is armed. The state every node boots into.</summary>
    Safe = 0,

    /// <summary>The sequence has started. This is the only state with a timed exit.</summary>
    Init = 1,

    Armed = 2,

    /// <summary>Latched. Only SAFE leaves this state.</summary>
    Fire = 3,
}
