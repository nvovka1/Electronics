using Initiator.DataAccess.Repositories;
using Initiator.Domain.Devices;
using Initiator.Domain.States;

namespace Initiator.Tests;

[Collection(MongoCollection.Name)]
public sealed class DeviceRepositoryTests
{
    private static readonly DateTime Now = new(2026, 9, 17, 12, 0, 0, DateTimeKind.Utc);

    private readonly MongoDeviceRepository _devices;

    public DeviceRepositoryTests(MongoFixture mongo)
    {
        // A database per test class, so one class's leftovers cannot decide
        // another's result.
        _devices = new MongoDeviceRepository(mongo.CreateContext($"devices-{Guid.NewGuid():N}"));
    }

    private static Device CheckIn(string serial, DateTime seenAt, int reboots = 1) => new()
    {
        Serial = serial,
        NodeId = 7,
        HardwareId = "ttgo-lora32-v21",
        FirmwareVersion = "1.0.0",
        AutoArmTimeoutSeconds = 300,
        LastSeenAt = seenAt,
        LastHealth = new DeviceHealth
        {
            ReceivedAt = seenAt,
            Reboots = reboots,
            BatteryDeciVolts = 39,
        },
    };

    [Fact]
    public async Task A_first_check_in_enrols_the_device()
    {
        var stored = await _devices.UpsertCheckInAsync(CheckIn("IN-0001", Now));

        Assert.Equal("IN-0001", stored.Serial);
        Assert.Equal(Now, stored.EnrolledAt);

        // Never restored from storage, and never anything else on a first sight.
        Assert.Equal(NodeState.Safe, stored.CurrentState);
    }

    [Fact]
    public async Task A_check_in_never_overwrites_what_the_operator_set()
    {
        // The test worth having. With a naive replace, a node reporting in would
        // wipe its own notes and its retirement.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0002", Now));
        await _devices.SetNotesAsync("IN-0002", "mast 3, call Yaroslav");
        await _devices.SetDecommissionedAsync("IN-0002", true);

        var after = await _devices.UpsertCheckInAsync(CheckIn("IN-0002", Now.AddMinutes(1)));

        Assert.Equal("mast 3, call Yaroslav", after.Notes);
        Assert.True(after.IsDecommissioned);
        Assert.Equal(Now.AddMinutes(1), after.LastSeenAt);
    }

    [Fact]
    public async Task Enrolment_time_survives_later_check_ins()
    {
        await _devices.UpsertCheckInAsync(CheckIn("IN-0003", Now));
        var after = await _devices.UpsertCheckInAsync(CheckIn("IN-0003", Now.AddHours(5)));

        Assert.Equal(Now, after.EnrolledAt);
    }

    [Fact]
    public async Task A_check_in_does_not_move_the_state_on_its_own()
    {
        // State goes through the guarded write. If a plain check-in could set it,
        // the guard would be bypassed by the most frequent request in the system.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0004", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0004", NodeState.Armed, CommandSource.Lora, 1, 5_000, Now);

        var after = await _devices.UpsertCheckInAsync(CheckIn("IN-0004", Now.AddMinutes(1)));

        Assert.Equal(NodeState.Armed, after.CurrentState);
    }

    // --- the ordering guard -------------------------------------------------

    [Fact]
    public async Task A_newer_report_moves_the_state()
    {
        await _devices.UpsertCheckInAsync(CheckIn("IN-0010", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0010", NodeState.Init, CommandSource.Lora, 1, 1_000, Now);

        var after = await _devices.ApplyReportedStateAsync(
            "IN-0010", NodeState.Armed, CommandSource.Timer, 1, 2_000, Now.AddSeconds(1));

        Assert.Equal(NodeState.Armed, after!.CurrentState);
        Assert.Equal(CommandSource.Timer, after.StateSource);
    }

    [Fact]
    public async Task A_report_that_arrives_late_does_not_put_the_state_back()
    {
        // The case this guard exists for: a health report in flight while the
        // node armed itself would otherwise return the dashboard to INIT and
        // leave it there until the next transition.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0011", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0011", NodeState.Armed, CommandSource.Timer, 1, 2_000, Now);

        var after = await _devices.ApplyReportedStateAsync(
            "IN-0011", NodeState.Init, CommandSource.Lora, 1, 1_000, Now.AddSeconds(1));

        Assert.Equal(NodeState.Armed, after!.CurrentState);
    }

    [Fact]
    public async Task A_reboot_wins_even_though_its_clock_restarted()
    {
        // After a reboot the node's clock is back near zero. Comparing
        // milliseconds alone would treat a freshly booted node as ancient and
        // ignore everything it ever said again.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0012", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0012", NodeState.Armed, CommandSource.Lora, 1, 900_000, Now);

        var after = await _devices.ApplyReportedStateAsync(
            "IN-0012", NodeState.Safe, CommandSource.Timer, 2, 50, Now.AddSeconds(1));

        Assert.Equal(NodeState.Safe, after!.CurrentState);
        Assert.Equal(2, after.StateBootCount);
    }

    [Fact]
    public async Task A_repeated_report_of_the_same_moment_is_accepted()
    {
        // The node retries. Rejecting an exact duplicate would be harmless here,
        // but accepting it keeps "same timestamp" from meaning "ignored", which
        // is the behaviour a retry depends on.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0013", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0013", NodeState.Init, CommandSource.Lora, 1, 1_000, Now);

        var after = await _devices.ApplyReportedStateAsync(
            "IN-0013", NodeState.Init, CommandSource.Lora, 1, 1_000, Now.AddSeconds(5));

        Assert.Equal(NodeState.Init, after!.CurrentState);
        Assert.Equal(Now.AddSeconds(5), after.StateChangedAt);
    }

    [Fact]
    public async Task Reporting_a_state_for_an_unknown_device_creates_nothing()
    {
        // Enrolment happens on a health report, not on a state event. A state
        // event for a serial nobody has heard of is a misconfigured node, and
        // inventing a device record for it would hide that.
        var result = await _devices.ApplyReportedStateAsync(
            "IN-NOBODY", NodeState.Fire, CommandSource.Lora, 1, 1_000, Now);

        Assert.Null(result);
        Assert.Null(await _devices.GetBySerialAsync("IN-NOBODY"));
    }

    [Fact]
    public async Task Deleting_removes_the_device()
    {
        await _devices.UpsertCheckInAsync(CheckIn("IN-0020", Now));
        await _devices.DeleteAsync("IN-0020");

        Assert.Null(await _devices.GetBySerialAsync("IN-0020"));
    }

    [Fact]
    public async Task Computed_properties_are_not_persisted()
    {
        // AutoArmDueAt is arithmetic over two stored fields. Persisting it would
        // let a stored copy disagree with the fields it is derived from.
        await _devices.UpsertCheckInAsync(CheckIn("IN-0021", Now));

        await _devices.ApplyReportedStateAsync(
            "IN-0021", NodeState.Init, CommandSource.Lora, 1, 1_000, Now);

        var stored = await _devices.GetBySerialAsync("IN-0021");

        Assert.Equal(Now.AddSeconds(300), stored!.AutoArmDueAt);
    }
}
