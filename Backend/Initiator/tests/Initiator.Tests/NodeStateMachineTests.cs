using Initiator.Domain.States;

namespace Initiator.Tests;

/// <summary>
/// The table from the design spec §1.1, asserted cell by cell. This is the one
/// test suite in the solution that is allowed to be repetitive: the whole point
/// is that somebody can read it against the spec without having to work
/// anything out.
/// </summary>
public sealed class NodeStateMachineTests
{
    // --- the table, cell by cell -------------------------------------------

    [Theory]
    // from            command              expected kind                        expected state
    [InlineData(NodeState.Safe, CommandType.Init, TransitionKind.Moved, NodeState.Init)]
    [InlineData(NodeState.Safe, CommandType.Arm, TransitionKind.Rejected, NodeState.Safe)]
    [InlineData(NodeState.Safe, CommandType.Fire, TransitionKind.Rejected, NodeState.Safe)]
    [InlineData(NodeState.Safe, CommandType.Safe, TransitionKind.NoOp, NodeState.Safe)]
    [InlineData(NodeState.Init, CommandType.Init, TransitionKind.CountdownRestarted, NodeState.Init)]
    [InlineData(NodeState.Init, CommandType.Arm, TransitionKind.Moved, NodeState.Armed)]
    [InlineData(NodeState.Init, CommandType.Fire, TransitionKind.Rejected, NodeState.Init)]
    [InlineData(NodeState.Init, CommandType.Safe, TransitionKind.Moved, NodeState.Safe)]
    [InlineData(NodeState.Armed, CommandType.Init, TransitionKind.Rejected, NodeState.Armed)]
    [InlineData(NodeState.Armed, CommandType.Arm, TransitionKind.NoOp, NodeState.Armed)]
    [InlineData(NodeState.Armed, CommandType.Fire, TransitionKind.Moved, NodeState.Fire)]
    [InlineData(NodeState.Armed, CommandType.Safe, TransitionKind.Moved, NodeState.Safe)]
    [InlineData(NodeState.Fire, CommandType.Init, TransitionKind.Rejected, NodeState.Fire)]
    [InlineData(NodeState.Fire, CommandType.Arm, TransitionKind.Rejected, NodeState.Fire)]
    [InlineData(NodeState.Fire, CommandType.Fire, TransitionKind.NoOp, NodeState.Fire)]
    [InlineData(NodeState.Fire, CommandType.Safe, TransitionKind.Moved, NodeState.Safe)]
    public void The_table_matches_the_spec(
        NodeState from, CommandType command, TransitionKind expectedKind, NodeState expectedState)
    {
        var result = NodeStateMachine.Apply(from, command);

        Assert.Equal(expectedKind, result.Kind);
        Assert.Equal(expectedState, result.State);
    }

    // --- the rules that fall out of it -------------------------------------

    [Fact]
    public void Boot_state_is_safe()
    {
        // Not a preference. A node that loses power mid-sequence and comes back
        // armed is the failure this constant exists to prevent.
        Assert.Equal(NodeState.Safe, NodeStateMachine.BootState);
    }

    [Theory]
    [InlineData(NodeState.Safe)]
    [InlineData(NodeState.Init)]
    [InlineData(NodeState.Armed)]
    [InlineData(NodeState.Fire)]
    public void Safe_is_accepted_from_every_state(NodeState from)
    {
        // SAFE is the only revoke path, so there must be no state it can be
        // refused from - including FIRE, which is latched.
        Assert.True(NodeStateMachine.Apply(from, CommandType.Safe).Accepted);
    }

    [Theory]
    [InlineData(NodeState.Init)]
    [InlineData(NodeState.Armed)]
    [InlineData(NodeState.Fire)]
    public void Safe_always_reaches_safe(NodeState from)
    {
        Assert.Equal(NodeState.Safe, NodeStateMachine.Apply(from, CommandType.Safe).State);
    }

    [Fact]
    public void Fire_is_only_reachable_from_armed()
    {
        var reachesFire = Enum.GetValues<NodeState>()
            .Where(state => NodeStateMachine.Apply(state, CommandType.Fire).State == NodeState.Fire)
            .ToArray();

        // FIRE from FIRE is a no-op that leaves the state at FIRE, so both the
        // no-op and the real transition are expected here.
        Assert.Equal([NodeState.Armed, NodeState.Fire], reachesFire);
    }

    [Fact]
    public void Armed_is_only_reachable_through_init()
    {
        var reachesArmed = Enum.GetValues<NodeState>()
            .Where(state => NodeStateMachine.Apply(state, CommandType.Arm).State == NodeState.Armed)
            .ToArray();

        Assert.Equal([NodeState.Init, NodeState.Armed], reachesArmed);
    }

    [Theory]
    [InlineData(NodeState.Safe, CommandType.Safe)]
    [InlineData(NodeState.Armed, CommandType.Arm)]
    [InlineData(NodeState.Fire, CommandType.Fire)]
    public void Re_sending_the_current_states_command_is_accepted_and_changes_nothing(
        NodeState state, CommandType command)
    {
        // The radio retries. A lost ACK must not turn a command that worked into
        // one that failed.
        var result = NodeStateMachine.Apply(state, command);

        Assert.True(result.Accepted);
        Assert.False(result.StateChanged);
        Assert.Equal(state, result.State);
        Assert.Equal(TransitionKind.NoOp, result.Kind);
    }

    [Fact]
    public void Init_while_in_init_is_accepted_and_restarts_the_countdown()
    {
        // The one no-op that does something. It is how an operator holds a node
        // in INIT while still setting up.
        var result = NodeStateMachine.Apply(NodeState.Init, CommandType.Init);

        Assert.True(result.Accepted);
        Assert.False(result.StateChanged);
        Assert.Equal(TransitionKind.CountdownRestarted, result.Kind);
    }

    [Fact]
    public void A_rejection_says_why()
    {
        var result = NodeStateMachine.Apply(NodeState.Safe, CommandType.Fire);

        Assert.False(result.Accepted);
        Assert.Equal(RejectReason.BadTransition, result.Reason);
    }

    [Fact]
    public void An_accepted_command_carries_no_reason()
    {
        Assert.Equal(RejectReason.Ok, NodeStateMachine.Apply(NodeState.Safe, CommandType.Init).Reason);
    }

    // --- the auto-arm -------------------------------------------------------

    [Fact]
    public void The_countdown_arms_a_node_that_is_in_init()
    {
        var result = NodeStateMachine.AutoArm(NodeState.Init);

        Assert.True(result.Accepted);
        Assert.Equal(NodeState.Armed, result.State);
    }

    [Theory]
    [InlineData(NodeState.Safe)]
    [InlineData(NodeState.Armed)]
    [InlineData(NodeState.Fire)]
    public void The_countdown_does_nothing_from_any_other_state(NodeState from)
    {
        // A node cannot arm itself out of SAFE. If this ever passes for SAFE,
        // a node sitting idle can arm itself with nobody having touched it.
        var result = NodeStateMachine.AutoArm(from);

        Assert.False(result.Accepted);
        Assert.Equal(from, result.State);
    }

    [Theory]
    [InlineData(NodeState.Safe, false)]
    [InlineData(NodeState.Init, true)]
    [InlineData(NodeState.Armed, false)]
    [InlineData(NodeState.Fire, false)]
    public void Only_init_starts_a_countdown(NodeState state, bool expected)
    {
        Assert.Equal(expected, NodeStateMachine.StartsCountdown(state));
    }

    // --- what the dashboard asks -------------------------------------------

    [Fact]
    public void A_node_in_safe_offers_init_and_safe()
    {
        Assert.Equal(
            [CommandType.Init, CommandType.Safe],
            NodeStateMachine.AllowedFrom(NodeState.Safe));
    }

    [Fact]
    public void A_node_in_init_offers_everything_but_fire()
    {
        Assert.Equal(
            [CommandType.Init, CommandType.Arm, CommandType.Safe],
            NodeStateMachine.AllowedFrom(NodeState.Init));
    }

    [Fact]
    public void A_node_in_armed_offers_arm_fire_and_safe()
    {
        Assert.Equal(
            [CommandType.Arm, CommandType.Fire, CommandType.Safe],
            NodeStateMachine.AllowedFrom(NodeState.Armed));
    }

    [Fact]
    public void A_node_in_fire_offers_only_fire_and_safe()
    {
        Assert.Equal(
            [CommandType.Fire, CommandType.Safe],
            NodeStateMachine.AllowedFrom(NodeState.Fire));
    }

    // --- values from outside the enums --------------------------------------

    [Fact]
    public void An_unknown_command_is_refused_rather_than_indexed()
    {
        // Comes off the wire as a byte. Reaching the table with it would be an
        // out-of-range index on a value an attacker chooses.
        var result = NodeStateMachine.Apply(NodeState.Armed, (CommandType)99);

        Assert.False(result.Accepted);
        Assert.Equal(RejectReason.BadCommand, result.Reason);
        Assert.Equal(NodeState.Armed, result.State);
    }

    [Fact]
    public void An_unknown_state_is_refused_rather_than_indexed()
    {
        var result = NodeStateMachine.Apply((NodeState)99, CommandType.Safe);

        Assert.False(result.Accepted);
        Assert.Equal(RejectReason.BadCommand, result.Reason);
    }
}
