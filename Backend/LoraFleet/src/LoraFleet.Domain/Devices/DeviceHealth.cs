namespace LoraFleet.Domain.Devices;

/// <summary>
/// The twelve bytes a node sends about itself, unpacked. It is the cheapest
/// diagnostic that exists - twelve bytes against a drive out - and the only one
/// that works while the node is still alive.
/// </summary>
public sealed class DeviceHealth
{
    public DateTime ReceivedAt { get; set; }

    /// <summary>Seconds since the node booted. A silent reboot is invisible without this.</summary>
    public long UptimeSeconds { get; set; }

    /// <summary>Total boots. Climbing between check-ins means something falls over systematically.</summary>
    public int Reboots { get; set; }

    /// <summary>esp_reset_reason() as a number; <see cref="LastCrashReason"/> is the decoded form.</summary>
    public int LastCrashCode { get; set; }

    public string LastCrashReason { get; set; } = string.Empty;

    /// <summary>Tenths of a volt, already calibrated. Zero means the node does not trust its own ADC.</summary>
    public int BatteryDeciVolts { get; set; }

    public int LastRssi { get; set; }

    /// <summary>POST result, one bit per block. Zero is a clean self-test.</summary>
    public int PostMask { get; set; }

    public bool BatteryIsTrusted => BatteryDeciVolts > 0;

    public double? BatteryVolts => BatteryIsTrusted ? BatteryDeciVolts / 10.0 : null;

    public bool PostPassed => PostMask == 0;
}
