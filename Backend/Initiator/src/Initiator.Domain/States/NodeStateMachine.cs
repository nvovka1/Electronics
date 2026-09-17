namespace Initiator.Domain.States;

/// <summary>
/// The Init/Arm/Fire/Safe rules. Pure: no Mongo, no HTTP, no clock. This is the
/// single place the backend knows what a node will do with a command.
/// </summary>
/// <remarks>
/// <para>
/// The firmware in <c>ESP32/initiator/explosion</c> holds the same table in C++.
/// The two are checked against each other by a test that parses the firmware's
/// header, because a table that drifts misbehaves in the field rather than
/// failing a build. See <c>docs/superpowers/specs/2026-09-17-initiator-system-design.md</c> §1.1.
/// </para>
/// <para>
/// This class does not run the auto-arm countdown and has no clock to run it
/// with. The countdown belongs to the node; duplicating it here would create a
/// second clock that can disagree with the one that matters. What this class
/// knows is that <see cref="AutoArm"/> is a legal transition, so a state event
/// reporting one can be accepted when it arrives.
/// </para>
/// </remarks>
public static class NodeStateMachine
{
    /// <summary>The state every node boots into. Never restored from storage.</summary>
    public const NodeState BootState = NodeState.Safe;

    /// <summary>
    /// One cell of the transition table: what the command does, and where it
    /// leaves the node.
    /// </summary>
    private readonly record struct Cell(TransitionKind Kind, NodeState Next);

    private static Cell Move(NodeState next) => new(TransitionKind.Moved, next);

    private static Cell NoOp(NodeState state) => new(TransitionKind.NoOp, state);

    private static Cell Restart(NodeState state) => new(TransitionKind.CountdownRestarted, state);

    private static Cell Reject(NodeState state) => new(TransitionKind.Rejected, state);

    /// <summary>
    /// The table from §1.1, in the same shape: a row per state, a column per
    /// command, in the order INIT, ARM, FIRE, SAFE. It is a literal table rather
    /// than a nest of conditionals so that it can be read against the spec cell
    /// by cell, which is the only way anyone will ever verify it.
    /// </summary>
    private static readonly Cell[,] Table =
    {
        //                    INIT                      ARM                     FIRE                     SAFE
        /* SAFE  */ { Move(NodeState.Init),     Reject(NodeState.Safe),  Reject(NodeState.Safe),  NoOp(NodeState.Safe)  },
        /* INIT  */ { Restart(NodeState.Init),  Move(NodeState.Armed),   Reject(NodeState.Init),  Move(NodeState.Safe)  },
        /* ARMED */ { Reject(NodeState.Armed),  NoOp(NodeState.Armed),   Move(NodeState.Fire),    Move(NodeState.Safe)  },
        /* FIRE  */ { Reject(NodeState.Fire),   Reject(NodeState.Fire),  NoOp(NodeState.Fire),    Move(NodeState.Safe)  },
    };

    /// <summary>
    /// Offer <paramref name="command"/> to a node in <paramref name="current"/>.
    /// </summary>
    public static TransitionResult Apply(NodeState current, CommandType command)
    {
        if (!IsDefined(current))
        {
            // A state outside the enum can only come from a corrupt record or a
            // firmware speaking a protocol this build does not know. Refusing is
            // the only answer that cannot make it worse.
            return new TransitionResult(TransitionKind.Rejected, current, RejectReason.BadCommand);
        }

        if (!IsDefined(command))
        {
            return new TransitionResult(TransitionKind.Rejected, current, RejectReason.BadCommand);
        }

        var cell = Table[(int)current, CommandColumn(command)];

        return new TransitionResult(
            cell.Kind,
            cell.Next,
            cell.Kind == TransitionKind.Rejected ? RejectReason.BadTransition : RejectReason.Ok);
    }

    /// <summary>
    /// The transition a node makes on its own when the INIT countdown expires.
    /// Legal from INIT and nowhere else.
    /// </summary>
    public static TransitionResult AutoArm(NodeState current) =>
        current == NodeState.Init
            ? new TransitionResult(TransitionKind.Moved, NodeState.Armed, RejectReason.Ok)
            : new TransitionResult(TransitionKind.Rejected, current, RejectReason.BadTransition);

    /// <summary>
    /// Whether entering this state starts the auto-arm countdown. INIT is the
    /// only state with a timed exit.
    /// </summary>
    public static bool StartsCountdown(NodeState state) => state == NodeState.Init;

    /// <summary>
    /// The commands that would be accepted right now — what the dashboard uses
    /// to decide which buttons are live. A stale view can make this wrong, which
    /// is why the node still judges every command it receives.
    /// </summary>
    public static IReadOnlyList<CommandType> AllowedFrom(NodeState current) =>
        [.. AllCommands.Where(command => Apply(current, command).Accepted)];

    /// <summary>In the table's column order, so a caller can render the row in the spec's order.</summary>
    public static readonly IReadOnlyList<CommandType> AllCommands =
        [CommandType.Init, CommandType.Arm, CommandType.Fire, CommandType.Safe];

    /// <summary>
    /// Maps a command onto its column. The commands are 1..4 and the columns
    /// 0..3, so this is a subtraction — written once, here, rather than as a
    /// <c>- 1</c> scattered through the file.
    /// </summary>
    private static int CommandColumn(CommandType command) => (int)command - 1;

    private static bool IsDefined(NodeState state) =>
        state is >= NodeState.Safe and <= NodeState.Fire;

    private static bool IsDefined(CommandType command) =>
        command is >= CommandType.Init and <= CommandType.Safe;
}
