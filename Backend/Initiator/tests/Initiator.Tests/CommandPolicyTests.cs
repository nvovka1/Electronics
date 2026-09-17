using Initiator.Domain.Commands;
using Initiator.Domain.States;

namespace Initiator.Tests;

public sealed class CommandPolicyTests
{
    private static readonly DateTime Now = new(2026, 9, 17, 12, 0, 0, DateTimeKind.Utc);

    private static Command Queued(TimeSpan ago, CommandStatus status = CommandStatus.Pending) => new()
    {
        DeviceSerial = "IN-TEST",
        Type = CommandType.Init,
        Status = status,
        QueuedAt = Now - ago,
    };

    [Fact]
    public void A_fresh_command_has_not_expired()
    {
        Assert.False(CommandPolicy.HasExpired(Queued(TimeSpan.FromMinutes(1)), Now));
    }

    [Fact]
    public void A_command_expires_once_it_has_waited_too_long()
    {
        Assert.True(CommandPolicy.HasExpired(Queued(CommandPolicy.ExpiresAfter), Now));
    }

    [Fact]
    public void Expiry_is_inclusive_at_the_boundary()
    {
        var justInside = CommandPolicy.ExpiresAfter - TimeSpan.FromSeconds(1);

        Assert.False(CommandPolicy.HasExpired(Queued(justInside), Now));
    }

    [Theory]
    [InlineData(CommandStatus.Delivered)]
    [InlineData(CommandStatus.Applied)]
    [InlineData(CommandStatus.Rejected)]
    [InlineData(CommandStatus.Cancelled)]
    [InlineData(CommandStatus.Expired)]
    public void Only_a_command_still_waiting_can_expire(CommandStatus status)
    {
        // A command the node already took cannot be un-taken by a clock here.
        var old = Queued(TimeSpan.FromHours(3), status);

        Assert.False(CommandPolicy.HasExpired(old, Now));
    }

    [Fact]
    public void Queueing_follows_the_state_machine()
    {
        Assert.True(CommandPolicy.CanQueue(NodeState.Armed, CommandType.Fire));
        Assert.False(CommandPolicy.CanQueue(NodeState.Safe, CommandType.Fire));
    }

    [Fact]
    public void Safe_can_always_be_queued()
    {
        // The dashboard must never be unable to offer a way out, whatever it
        // believes the node is doing.
        foreach (var state in Enum.GetValues<NodeState>())
        {
            Assert.True(CommandPolicy.CanQueue(state, CommandType.Safe));
        }
    }

    [Fact]
    public void A_command_is_terminal_once_resolved()
    {
        Assert.False(Queued(TimeSpan.Zero, CommandStatus.Pending).IsTerminal);
        Assert.False(Queued(TimeSpan.Zero, CommandStatus.Delivered).IsTerminal);
        Assert.True(Queued(TimeSpan.Zero, CommandStatus.Applied).IsTerminal);
        Assert.True(Queued(TimeSpan.Zero, CommandStatus.Rejected).IsTerminal);
        Assert.True(Queued(TimeSpan.Zero, CommandStatus.Expired).IsTerminal);
        Assert.True(Queued(TimeSpan.Zero, CommandStatus.Cancelled).IsTerminal);
    }
}
