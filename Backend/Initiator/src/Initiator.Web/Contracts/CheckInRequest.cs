namespace Initiator.Web.Contracts;

/// <summary>
/// What a node reports about itself, on a period. The first one from a serial
/// enrols the device.
/// </summary>
/// <remarks>
/// States, commands and reasons travel as numbers here, not names. They are the
/// same bytes the LoRa protocol uses, and the firmware has them as an enum
/// already — asking it to build strings would cost flash and invent a second
/// spelling of something that already has one.
/// </remarks>
public sealed class CheckInRequest
{
    /// <summary>Protocol address, the <c>dst</c> a controller puts in a command frame.</summary>
    public int NodeId { get; set; }

    public string HardwareId { get; set; } = string.Empty;

    public string FirmwareVersion { get; set; } = string.Empty;

    public string FirmwareHash { get; set; } = string.Empty;

    public bool FirmwareIsDirty { get; set; }

    public string BuildType { get; set; } = string.Empty;

    public int ProtocolVersion { get; set; }

    public long UptimeSeconds { get; set; }

    /// <summary>Total boots. Also orders state reports — see <c>Device.StateBootCount</c>.</summary>
    public int Reboots { get; set; }

    public int LastCrashCode { get; set; }

    /// <summary>Tenths of a volt. Zero means the node does not trust its own ADC.</summary>
    public int BatteryDeciVolts { get; set; }

    public int LastRssi { get; set; }

    /// <summary>Self-test result, one bit per block. Zero is clean.</summary>
    public int PostMask { get; set; }

    public bool LoraLinkUp { get; set; }

    // --- state resync -------------------------------------------------------

    /// <summary>
    /// The state the node is in right now, as a <c>NodeState</c> value. Carried
    /// on every check-in and not only when it changes, so a dashboard that
    /// missed a state event is corrected within one report rather than staying
    /// wrong until the next transition.
    /// </summary>
    public byte State { get; set; }

    /// <summary>The node's monotonic clock when it entered that state.</summary>
    public long StateTimestampMs { get; set; }

    /// <summary>
    /// The node's configured auto-arm timeout. Reported rather than assumed
    /// because it is settable per node, so a node that was tuned would otherwise
    /// be shown someone else's countdown.
    /// </summary>
    public int AutoArmTimeoutSeconds { get; set; }
}
