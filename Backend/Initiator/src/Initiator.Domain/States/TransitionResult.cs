namespace Initiator.Domain.States;

/// <summary>The outcome of offering one command to a node in one state.</summary>
/// <param name="Kind">What happened.</param>
/// <param name="State">The state afterwards. Unchanged unless <see cref="TransitionKind.Moved"/>.</param>
/// <param name="Reason">Why, when it was refused. <see cref="RejectReason.Ok"/> otherwise.</param>
public readonly record struct TransitionResult(
    TransitionKind Kind,
    NodeState State,
    RejectReason Reason)
{
    /// <summary>
    /// True for every outcome but a rejection — including both no-ops. Code
    /// that only wants to know "did this work" should ask this rather than
    /// comparing states, because a successful command often leaves the state
    /// alone.
    /// </summary>
    public bool Accepted => Kind != TransitionKind.Rejected;

    /// <summary>Whether this outcome is worth telling anyone about a state change.</summary>
    public bool StateChanged => Kind == TransitionKind.Moved;
}
