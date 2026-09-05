using LoraFleet.Domain.Devices;

namespace LoraFleet.Tests;

public sealed class DeviceStatusPolicyTests
{
    private static readonly DateTime Now = new(2026, 9, 5, 12, 0, 0, DateTimeKind.Utc);
    private static readonly TimeSpan ReportPeriod = TimeSpan.FromSeconds(60);

    private static Device DeviceLastSeen(TimeSpan ago, int postMask = 0, int batteryDeciVolts = 39) => new()
    {
        Serial = "LQ-TEST",
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
        var device = DeviceLastSeen(TimeSpan.FromSeconds(30));

        Assert.Equal(DeviceStatus.Healthy, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void One_missed_report_is_still_healthy()
    {
        // A single dropped uplink is normal on any radio link and must not turn
        // the whole fleet amber.
        var device = DeviceLastSeen(TimeSpan.FromSeconds(90));

        Assert.Equal(DeviceStatus.Healthy, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void Beyond_two_missed_reports_is_late()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(150));

        Assert.Equal(DeviceStatus.Late, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void Two_hours_of_silence_is_missing()
    {
        var device = DeviceLastSeen(TimeSpan.FromHours(2));

        Assert.Equal(DeviceStatus.Missing, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void Silence_outranks_a_clean_post()
    {
        // The last frame said everything was fine, and then it stopped talking.
        // Reporting that as healthy is exactly the silence that costs trust.
        var device = DeviceLastSeen(TimeSpan.FromHours(6), postMask: 0);

        Assert.Equal(DeviceStatus.Missing, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_failed_post_block_is_degraded()
    {
        var device = DeviceLastSeen(TimeSpan.FromSeconds(10), postMask: 0x0008);

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void An_untrusted_battery_is_degraded()
    {
        // Zero decivolts is the node saying it does not believe its own ADC,
        // which also means its low-power write gate is disabled.
        var device = DeviceLastSeen(TimeSpan.FromSeconds(10), batteryDeciVolts: 0);

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_device_that_has_never_reported_health_is_degraded()
    {
        var device = new Device { Serial = "LQ-NEW", LastSeenAt = Now };

        Assert.Equal(DeviceStatus.Degraded, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
    }

    [Fact]
    public void A_retired_node_is_never_reported_as_missing()
    {
        var device = DeviceLastSeen(TimeSpan.FromDays(30));
        device.IsDecommissioned = true;

        Assert.Equal(DeviceStatus.Decommissioned, DeviceStatusPolicy.Evaluate(device, ReportPeriod, Now));
        Assert.False(DeviceStatusPolicy.NeedsAttention(DeviceStatus.Decommissioned));
    }

    [Theory]
    [InlineData(DeviceStatus.Degraded)]
    [InlineData(DeviceStatus.Late)]
    [InlineData(DeviceStatus.Missing)]
    public void Anything_wrong_needs_attention(DeviceStatus status)
    {
        Assert.True(DeviceStatusPolicy.NeedsAttention(status));
    }

    [Fact]
    public void Out_of_date_needs_both_a_target_and_a_difference()
    {
        var device = new Device { Serial = "LQ-TEST", FirmwareVersion = "1.0.0" };
        Assert.False(device.IsOutOfDate);

        device.TargetFirmwareVersion = "1.0.0";
        Assert.False(device.IsOutOfDate);

        device.TargetFirmwareVersion = "1.1.0";
        Assert.True(device.IsOutOfDate);
    }
}
