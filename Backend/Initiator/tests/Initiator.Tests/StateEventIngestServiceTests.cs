using Initiator.DataAccess;
using Initiator.DataAccess.Repositories;
using Initiator.Domain.Commands;
using Initiator.Domain.Devices;
using Initiator.Domain.States;
using Initiator.Web.Contracts;
using Initiator.Web.Services;
using Microsoft.Extensions.Logging.Abstractions;

namespace Initiator.Tests;

[Collection(MongoCollection.Name)]
public sealed class StateEventIngestServiceTests
{
    private static readonly DateTime Now = new(2026, 9, 17, 12, 0, 0, DateTimeKind.Utc);

    private readonly InitiatorDbContext _context;
    private readonly MongoDeviceRepository _devices;
    private readonly MongoCommandRepository _commands;
    private readonly MongoStateEventRepository _events;
    private readonly StateEventIngestService _ingest;

    public StateEventIngestServiceTests(MongoFixture mongo)
    {
        _context = mongo.CreateContext($"ingest-{Guid.NewGuid():N}");
        _devices = new MongoDeviceRepository(_context);
        _commands = new MongoCommandRepository(_context);
        _events = new MongoStateEventRepository(_context);

        _ingest = new StateEventIngestService(
            _events,
            _devices,
            _commands,
            new FakeClock(Now),
            NullLogger<StateEventIngestService>.Instance);
    }

    private sealed class FakeClock(DateTime now) : TimeProvider
    {
        public override DateTimeOffset GetUtcNow() => new(now, TimeSpan.Zero);
    }

    private async Task EnrolAsync(string serial) =>
        await _devices.UpsertCheckInAsync(new Device { Serial = serial, LastSeenAt = Now });

    private static StateEventRequest Event(
        NodeState from,
        NodeState to,
        CommandType? command,
        CommandSource source,
        long timestampMs,
        bool accepted = true,
        int bootCount = 1,
        string? commandId = null) => new()
    {
        TimestampMs = timestampMs,
        BootCount = bootCount,
        FromState = (byte)from,
        ToState = (byte)to,
        Command = command is null ? null : (byte)command,
        Source = (byte)source,
        Accepted = accepted,
        Reason = (byte)(accepted ? RejectReason.Ok : RejectReason.BadTransition),
        CommandId = commandId,
    };

    [Fact]
    public async Task A_batch_is_stored_and_moves_the_device()
    {
        await EnrolAsync("IN-0200");

        var outcome = await _ingest.RecordAsync("IN-0200", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Safe, NodeState.Init, CommandType.Init, CommandSource.Lora, 1_000),
                Event(NodeState.Init, NodeState.Armed, CommandType.Arm, CommandSource.Lora, 2_000),
            ],
        });

        Assert.Equal(2, outcome.Stored);
        Assert.Equal(NodeState.Armed, (await _devices.GetBySerialAsync("IN-0200"))!.CurrentState);
    }

    [Fact]
    public async Task The_newest_event_wins_even_when_the_batch_arrives_out_of_order()
    {
        // A node flushing a backlog sends these oldest-first, but nothing in the
        // protocol guarantees it, and taking the last element of the list rather
        // than the newest one would leave the dashboard a step behind.
        await EnrolAsync("IN-0201");

        await _ingest.RecordAsync("IN-0201", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Init, NodeState.Armed, CommandType.Arm, CommandSource.Lora, 2_000),
                Event(NodeState.Safe, NodeState.Init, CommandType.Init, CommandSource.Lora, 1_000),
            ],
        });

        Assert.Equal(NodeState.Armed, (await _devices.GetBySerialAsync("IN-0201"))!.CurrentState);
    }

    [Fact]
    public async Task An_auto_arm_is_recorded_with_no_command_behind_it()
    {
        await EnrolAsync("IN-0202");

        await _ingest.RecordAsync("IN-0202", new StateEventBatchRequest
        {
            Events = [Event(NodeState.Init, NodeState.Armed, null, CommandSource.Timer, 5_000)],
        });

        var stored = await _events.QueryAsync("IN-0202", 10);
        var only = stored.Single();

        Assert.True(only.IsAutoArm);
        Assert.Null(only.Command);
        Assert.Equal(CommandSource.Timer, only.Source);
        Assert.Equal(NodeState.Armed, (await _devices.GetBySerialAsync("IN-0202"))!.CurrentState);
    }

    [Fact]
    public async Task A_refused_command_is_kept_and_still_reports_the_state()
    {
        // The rejections are usually the interesting records, and a refused
        // command still tells us where the node actually is.
        await EnrolAsync("IN-0203");

        await _ingest.RecordAsync("IN-0203", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Safe, NodeState.Safe, CommandType.Fire, CommandSource.Lora, 1_000,
                      accepted: false),
            ],
        });

        var only = (await _events.QueryAsync("IN-0203", 10)).Single();

        Assert.False(only.Accepted);
        Assert.Equal(RejectReason.BadTransition, only.Reason);
        Assert.Equal(NodeState.Safe, (await _devices.GetBySerialAsync("IN-0203"))!.CurrentState);
    }

    [Fact]
    public async Task An_event_carrying_a_state_this_build_does_not_know_is_discarded()
    {
        // Storing it would put a value on the dashboard that renders as
        // "state200", and the first anyone would know is a confused screenshot.
        await EnrolAsync("IN-0204");

        var outcome = await _ingest.RecordAsync("IN-0204", new StateEventBatchRequest
        {
            Events =
            [
                new StateEventRequest
                {
                    TimestampMs = 1_000,
                    BootCount = 1,
                    FromState = (byte)NodeState.Safe,
                    ToState = 200,
                    Source = (byte)CommandSource.Lora,
                    Accepted = true,
                },
            ],
        });

        Assert.Equal(0, outcome.Stored);
        Assert.Equal(1, outcome.Discarded);
        Assert.Empty(await _events.QueryAsync("IN-0204", 10));
    }

    [Fact]
    public async Task A_good_event_in_a_batch_with_a_bad_one_still_lands()
    {
        await EnrolAsync("IN-0205");

        var outcome = await _ingest.RecordAsync("IN-0205", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Safe, NodeState.Init, CommandType.Init, CommandSource.Lora, 1_000),
                new StateEventRequest { ToState = 200, FromState = 200, BootCount = 1 },
            ],
        });

        Assert.Equal(1, outcome.Stored);
        Assert.Equal(1, outcome.Discarded);
    }

    [Fact]
    public async Task An_event_closes_the_command_it_names()
    {
        await EnrolAsync("IN-0206");

        var queued = await _commands.QueueAsync(new Command
        {
            DeviceSerial = "IN-0206",
            Type = CommandType.Init,
            Status = CommandStatus.Pending,
            QueuedAt = Now,
            QueuedAgainstState = NodeState.Safe,
        });

        await _commands.ClaimNextForDeviceAsync("IN-0206", Now);

        await _ingest.RecordAsync("IN-0206", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Safe, NodeState.Init, CommandType.Init, CommandSource.Api, 1_000,
                      commandId: queued.Id),
            ],
        });

        Assert.Equal(CommandStatus.Applied, (await _commands.GetByIdAsync(queued.Id))!.Status);
    }

    [Fact]
    public async Task A_refused_command_is_closed_as_rejected()
    {
        await EnrolAsync("IN-0207");

        var queued = await _commands.QueueAsync(new Command
        {
            DeviceSerial = "IN-0207",
            Type = CommandType.Fire,
            Status = CommandStatus.Pending,
            QueuedAt = Now,
            QueuedAgainstState = NodeState.Armed,
        });

        await _commands.ClaimNextForDeviceAsync("IN-0207", Now);

        await _ingest.RecordAsync("IN-0207", new StateEventBatchRequest
        {
            Events =
            [
                Event(NodeState.Safe, NodeState.Safe, CommandType.Fire, CommandSource.Api, 1_000,
                      accepted: false, commandId: queued.Id),
            ],
        });

        var settled = await _commands.GetByIdAsync(queued.Id);

        Assert.Equal(CommandStatus.Rejected, settled!.Status);
        Assert.Equal(RejectReason.BadTransition, settled.Reason);
    }

    [Fact]
    public async Task An_empty_batch_does_nothing_rather_than_failing()
    {
        await EnrolAsync("IN-0208");

        var outcome = await _ingest.RecordAsync("IN-0208", new StateEventBatchRequest());

        Assert.Equal(0, outcome.Stored);
        Assert.Equal(NodeState.Safe, (await _devices.GetBySerialAsync("IN-0208"))!.CurrentState);
    }
}
