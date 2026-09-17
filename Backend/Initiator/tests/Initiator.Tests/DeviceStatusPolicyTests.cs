using Initiator.Domain.Devices;
using Initiator.Domain.States;

namespace Initiator.Tests;

public sealed class DeviceStatusPolicyTests
{
    private static readonly DateTime Now = new(2026, 9, 17, 12, 0, 0, DateTimeKind.Utc);
    private static readonly TimeSpan ReportPeriod = TimeSpan.FromSeconds(30);

    private static Device DeviceLastSeen(
        TimeSpan ago, int postMask = 0, int batteryDeciVolts = 39) => new()
    {
        Serial = "IN-TEST",
        LastSeenAt = Now - ago,
        LastHealth = new DeviceHealth
        {
            ReceivedAt = Now - ago,
            PostMask = postMask,
            BatteryDeciVolts = batteryDeciVolts,
        },
    };

    [Fact]
    public void A_recent_clean_check_in_is_healthy()
    {
        Assert.Equal(
            DeviceStatus.Healthy,
            DeviceStatusPolicy.Evaluate(DeviceLastSeen(TimeSpan.FromSeconds(10)), ReportPeriod, Now));
    }

    [Fact]
    public void One_missed_report_is_still_healthy()
    {
        // A single dropped request is normal over WiFi on a battery and must not
        // turn the whole fleet amber.
        Assert.Equal(
            DeviceStatus.Healthy,
            DeviceStatusPolicy.Evaluate(DeviceLastSeen(TimeSpan.FromSeconds(45)), ReportPeriod, Now));
    }

    [Fact]
    public void Three_missed_reports_is_late()
    {
        Assert.Equal(
            DeviceStatus.Late,
            DeviceStatusPolicy.Evaluate(DeviceLastSeen(TimeSpan.FromSeconds(95)), ReportPeriod, Now));
    }

    [Fact]
    public void Long_silence_is_missing()
    {
        Assert.Equal(
            DeviceStatus.Missing,
            DeviceStatusPolicy.Evaluate(DeviceLastSeen(DeviceStatusPolicy.MissingAfter), ReportPeriod, Now));
    }

    [Fact]
    public void Silence_outranks_a_clean_self_test()
    {
        // A node that reported clean and then stopped talking is not healthy,
        // whatever its last report said.
        var device = DeviceLastSeen(TimeSpan.FromHours(2), postMask: 0);

        Assert.Equal(DeviceStatus.Missing, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_failed_self_test_is_degraded()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5), postMask: 0b10);

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void An_untrusted_battery_is_degraded()
    {
        // Zero is not a flat battery: it is the node saying it cannot measure
        // its own supply.
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5), batteryDeciVolts: 0);

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_device_that_has_never_reported_health_is_degraded()
    {
        var device = new Device { Serial = "IN-TEST", LastSeenAt = Now };

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_decommissioned_device_is_never_missing()
    {
        var device = DeviceLastSeen(TimeSpan.FromDays(30));
        device.IsDecommissioned = true;

        Assert.Equal(
            DeviceStatus.Decommissioned, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_node_not_in_safe_is_live()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = NodeState.Armed;

        Assert.True(DeviceStatusPolicy.IsLive(device));
    }

    [Fact]
    public void A_node_in_safe_is_not_live()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = NodeState.Safe;

        Assert.False(DeviceStatusPolicy.IsLive(device));
    }

    [Fact]
    public void A_decommissioned_node_is_not_live_whatever_state_it_last_reported()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = NodeState.Fire;
        device.IsDecommissioned = true;

        Assert.False(DeviceStatusPolicy.IsLive(device));
    }

    // --- the countdown estimate --------------------------------------------

    [Fact]
    public void A_node_in_init_has_an_expected_auto_arm_time()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = NodeState.Init;
        device.StateChangedAt = Now;
        device.AutoArmTimeoutSeconds = 300;

        Assert.Equal(Now.AddSeconds(300), device.AutoArmDueAt);
    }

    [Theory]
    [InlineData(NodeState.Safe)]
    [InlineData(NodeState.Armed)]
    [InlineData(NodeState.Fire)]
    public void No_other_state_has_a_countdown(NodeState state)
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = state;
        device.AutoArmTimeoutSeconds = 300;

        Assert.Null(device.AutoArmDueAt);
    }

    [Fact]
    public void A_node_that_has_not_reported_its_timeout_shows_no_countdown()
    {
        // Better no estimate than one built on a guess at the node's setting.
        var device = DeviceLastSeen(TimeSpan.FromSeconds(5));
        device.CurrentState = NodeState.Init;
        device.AutoArmTimeoutSeconds = 0;

        Assert.Null(device.AutoArmDueAt);
    }
}
