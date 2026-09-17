namespace Initiator.Domain.States;

/// <summary>
/// What a command did, beyond whether it was accepted. Three of these four are
/// accepted outcomes, which is why "accepted" alone is not enough to describe a
/// transition.
/// </summary>
public enum TransitionKind
{
    /// <summary>The state changed.</summary>
    Moved,

    /// <summary>
    /// The command for the state the node was already in. Accepted and changed
    /// nothing. The radio retries, so a lost ACK must not turn a successful
    /// command into a failed one.
    /// </summary>
    NoOp,

    /// <summary>
    /// INIT while already in INIT. Accepted, the state did not change, and the
    /// auto-arm countdown started again — the one no-op that does something.
    /// </summary>
    CountdownRestarted,

    /// <summary>Refused. The state did not change.</summary>
    Rejected,
}
