using Initiator.DataAccess.Repositories;
using Initiator.Domain.Commands;
using Initiator.Domain.States;

namespace Initiator.Tests;

[Collection(MongoCollection.Name)]
public sealed class CommandRepositoryTests
{
    private static readonly DateTime Now = new(2026, 9, 17, 12, 0, 0, DateTimeKind.Utc);

    private readonly MongoCommandRepository _commands;

    public CommandRepositoryTests(MongoFixture mongo)
    {
        _commands = new MongoCommandRepository(mongo.CreateContext($"commands-{Guid.NewGuid():N}"));
    }

    private async Task<Command> QueueAsync(
        string serial, CommandType type, DateTime? queuedAt = null)
    {
        return await _commands.QueueAsync(new Command
        {
            DeviceSerial = serial,
            Type = type,
            Status = CommandStatus.Pending,
            QueuedAt = queuedAt ?? Now,
            QueuedAgainstState = NodeState.Safe,
        });
    }

    [Fact]
    public async Task A_queued_command_gets_an_id()
    {
        var command = await QueueAsync("IN-0100", CommandType.Init);

        // The node quotes this back when reporting the result, so an empty one
        // would silently break the whole resolution path.
        Assert.False(string.IsNullOrWhiteSpace(command.Id));
    }

    [Fact]
    public async Task Claiming_returns_nothing_when_the_queue_is_empty()
    {
        Assert.Null(await _commands.ClaimNextForDeviceAsync("IN-0101", Now));
    }

    [Fact]
    public async Task Claiming_marks_the_command_delivered()
    {
        await QueueAsync("IN-0102", CommandType.Init);

        var claimed = await _commands.ClaimNextForDeviceAsync("IN-0102", Now);

        Assert.NotNull(claimed);
        Assert.Equal(CommandStatus.Delivered, claimed.Status);
        Assert.Equal(Now, claimed.DeliveredAt);
    }

    [Fact]
    public async Task A_command_is_only_ever_handed_out_once()
    {
        // Two polls in flight at once, or two instances of the service behind a
        // load balancer, must not both be given the same command.
        await QueueAsync("IN-0103", CommandType.Init);

        var first = await _commands.ClaimNextForDeviceAsync("IN-0103", Now);
        var second = await _commands.ClaimNextForDeviceAsync("IN-0103", Now);

        Assert.NotNull(first);
        Assert.Null(second);
    }

    [Fact]
    public async Task Commands_are_handed_out_oldest_first()
    {
        // Clicking Init then Arm and having the node see them the other way
        // round would be a rejection for no reason the operator can see.
        await QueueAsync("IN-0104", CommandType.Init, Now);
        await QueueAsync("IN-0104", CommandType.Arm, Now.AddSeconds(5));

        var first = await _commands.ClaimNextForDeviceAsync("IN-0104", Now.AddSeconds(10));
        var second = await _commands.ClaimNextForDeviceAsync("IN-0104", Now.AddSeconds(10));

        Assert.Equal(CommandType.Init, first!.Type);
        Assert.Equal(CommandType.Arm, second!.Type);
    }

    [Fact]
    public async Task A_command_for_another_device_is_not_handed_over()
    {
        await QueueAsync("IN-0105", CommandType.Fire);

        Assert.Null(await _commands.ClaimNextForDeviceAsync("IN-0106", Now));
    }

    [Fact]
    public async Task A_command_that_waited_too_long_expires_instead_of_being_delivered()
    {
        // A node offline for an hour must not come back and act on an INIT
        // somebody clicked at the start of it.
        await QueueAsync("IN-0107", CommandType.Init, Now);

        var later = Now + CommandPolicy.ExpiresAfter + TimeSpan.FromMinutes(1);
        var claimed = await _commands.ClaimNextForDeviceAsync("IN-0107", later);

        Assert.Null(claimed);

        var history = await _commands.QueryAsync("IN-0107", 10);
        Assert.Equal(CommandStatus.Expired, history.Single().Status);
    }

    [Fact]
    public async Task Resolving_records_what_the_node_did()
    {
        var queued = await QueueAsync("IN-0110", CommandType.Arm);
        await _commands.ClaimNextForDeviceAsync("IN-0110", Now);

        var resolved = await _commands.ResolveAsync(
            queued.Id, CommandStatus.Applied, RejectReason.Ok, Now.AddSeconds(2));

        Assert.Equal(CommandStatus.Applied, resolved!.Status);
        Assert.Equal(Now.AddSeconds(2), resolved.ResolvedAt);
    }

    [Fact]
    public async Task A_rejection_keeps_the_reason()
    {
        var queued = await QueueAsync("IN-0111", CommandType.Fire);
        await _commands.ClaimNextForDeviceAsync("IN-0111", Now);

        var resolved = await _commands.ResolveAsync(
            queued.Id, CommandStatus.Rejected, RejectReason.BadTransition, Now);

        Assert.Equal(RejectReason.BadTransition, resolved!.Reason);
    }

    [Fact]
    public async Task A_settled_command_cannot_be_resolved_again()
    {
        // A node retrying a result post it never saw acknowledged would
        // otherwise rewrite a settled record.
        var queued = await QueueAsync("IN-0112", CommandType.Init);
        await _commands.ClaimNextForDeviceAsync("IN-0112", Now);
        await _commands.ResolveAsync(queued.Id, CommandStatus.Applied, RejectReason.Ok, Now);

        var again = await _commands.ResolveAsync(
            queued.Id, CommandStatus.Rejected, RejectReason.BadTransition, Now.AddMinutes(1));

        Assert.Null(again);

        var stored = await _commands.GetByIdAsync(queued.Id);
        Assert.Equal(CommandStatus.Applied, stored!.Status);
    }

    [Fact]
    public async Task A_pending_command_can_be_withdrawn()
    {
        var queued = await QueueAsync("IN-0120", CommandType.Init);

        Assert.True(await _commands.CancelAsync(queued.Id, Now));
        Assert.Equal(CommandStatus.Cancelled, (await _commands.GetByIdAsync(queued.Id))!.Status);
    }

    [Fact]
    public async Task A_delivered_command_cannot_be_withdrawn()
    {
        // Once the node has it, this service has no way to recall it. Saying
        // "cancelled" would be a comforting lie.
        var queued = await QueueAsync("IN-0121", CommandType.Init);
        await _commands.ClaimNextForDeviceAsync("IN-0121", Now);

        Assert.False(await _commands.CancelAsync(queued.Id, Now));
        Assert.Equal(CommandStatus.Delivered, (await _commands.GetByIdAsync(queued.Id))!.Status);
    }

    [Fact]
    public async Task Deleting_a_device_removes_its_commands()
    {
        await QueueAsync("IN-0130", CommandType.Init);
        await _commands.DeleteForDeviceAsync("IN-0130");

        Assert.Empty(await _commands.QueryAsync("IN-0130", 10));
    }
}
