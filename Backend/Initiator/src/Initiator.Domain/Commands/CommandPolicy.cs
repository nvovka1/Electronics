using Initiator.Domain.States;

namespace Initiator.Domain.Commands;

/// <summary>
/// The rules about queued commands that are not the state machine's business:
/// how long one may wait to be collected, and whether queueing it is worth
/// trying at all.
/// </summary>
/// <remarks>
/// Pure functions of a command and a clock reading, so every boundary is a host
/// test and none of it needs a database.
/// </remarks>
public static class CommandPolicy
{
    /// <summary>
    /// How long a command may sit unclaimed before it is abandoned. Long enough
    /// to cover a node that missed a few polls, short enough that nobody is
    /// surprised by one arriving.
    /// </summary>
    public static readonly TimeSpan ExpiresAfter = TimeSpan.FromMinutes(10);

    public static bool HasExpired(Command command, DateTime utcNow)
    {
        ArgumentNullException.ThrowIfNull(command);

        return command.IsOutstanding && utcNow - command.QueuedAt >= ExpiresAfter;
    }

    /// <summary>
    /// Whether it is worth queueing <paramref name="type"/> for a node currently
    /// believed to be in <paramref name="believedState"/>.
    /// </summary>
    /// <remarks>
    /// This is a courtesy, not an authority. The node judges every command it
    /// receives against its own state, which may have moved on — by an auto-arm,
    /// or by the handheld controller — since this service last heard from it.
    /// Refusing here just means the operator finds out immediately instead of
    /// one poll interval later.
    /// </remarks>
    public static bool CanQueue(NodeState believedState, CommandType type) =>
        NodeStateMachine.Apply(believedState, type).Accepted;
}
