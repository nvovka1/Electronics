namespace Initiator.Domain.Devices;

/// <summary>
/// What a node says about itself when it checks in. The cheapest diagnostic
/// there is, and the only one that works while the node is still alive.
/// </summary>
public sealed class DeviceHealth
{
    public DateTime ReceivedAt { get; set; }

    /// <summary>Seconds since the node booted. A silent reboot is invisible without this.</summary>
    public long UptimeSeconds { get; set; }

    /// <summary>Total boots. Climbing between check-ins means something falls over systematically.</summary>
    public int Reboots { get; set; }

    /// <summary><c>esp_reset_reason()</c> as a number.</summary>
    public int LastCrashCode { get; set; }

    /// <summary>Tenths of a volt, already calibrated. Zero means the node does not trust its own ADC.</summary>
    public int BatteryDeciVolts { get; set; }

    /// <summary>dBm of the last frame the node received.</summary>
    public int LastRssi { get; set; }

    /// <summary>Power-on self-test result, one bit per block. Zero is clean.</summary>
    public int PostMask { get; set; }

    /// <summary>
    /// Whether the node has a usable radio link to a controller. A node can be
    /// perfectly healthy over WiFi and deaf on LoRa, and the difference decides
    /// whether the handheld will work.
    /// </summary>
    public bool LoraLinkUp { get; set; }

    /// <summary>
    /// Zero is not a flat battery: it is the node reporting that its own ADC
    /// self-test failed, so the reading must not be believed.
    /// </summary>
    public bool BatteryIsTrusted => BatteryDeciVolts > 0;

    public double? BatteryVolts => BatteryIsTrusted ? BatteryDeciVolts / 10.0 : null;

    public bool PostPassed => PostMask == 0;
}
