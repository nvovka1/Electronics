using System.ComponentModel.DataAnnotations;

namespace LoraFleet.Web.Contracts;

/// <summary>
/// What a node posts about itself. The same facts the `version` and `health`
/// commands print over UART - same data, different wrapper.
/// </summary>
/// <remarks>
/// Every field is validated here rather than trusted. A device on the far end
/// of a radio link is not a trusted client: it can be old, mid-reboot, or not
/// our device at all.
/// </remarks>
public sealed class CheckInRequest
{
    [Range(0, 999)]
    public int NodeId { get; set; }

    [MaxLength(64)]
    public string HardwareId { get; set; } = string.Empty;

    [MaxLength(32)]
    public string FirmwareVersion { get; set; } = string.Empty;

    [MaxLength(16)]
    public string FirmwareHash { get; set; } = string.Empty;

    public bool FirmwareIsDirty { get; set; }

    [MaxLength(16)]
    public string BuildType { get; set; } = string.Empty;

    [Range(0, 255)]
    public int ProtocolVersion { get; set; }

    [Range(0, 65535)]
    public int ConfigVersion { get; set; }

    /// <summary>Seconds since the node booted. Its own monotonic clock, not wall-clock time.</summary>
    [Range(0, long.MaxValue)]
    public long UptimeSeconds { get; set; }

    [Range(0, int.MaxValue)]
    public int Reboots { get; set; }

    [Range(0, 255)]
    public int LastCrashCode { get; set; }

    /// <summary>Tenths of a volt. Zero means the node does not trust its own ADC.</summary>
    [Range(0, 255)]
    public int BatteryDeciVolts { get; set; }

    [Range(-200, 50)]
    public int LastRssi { get; set; }

    /// <summary>POST bitmask, one bit per block. Zero is a clean self-test.</summary>
    [Range(0, 65535)]
    public int PostMask { get; set; }
}
