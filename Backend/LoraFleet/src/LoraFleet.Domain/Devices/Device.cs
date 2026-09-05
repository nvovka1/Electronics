namespace LoraFleet.Domain.Devices;

/// <summary>
/// One physical node in the fleet. A device enrols itself the first time it
/// checks in: the serial number is printed on the case, travels in every frame
/// and is the key into this record, so there is nothing to type by hand.
/// </summary>
public sealed class Device
{
    /// <summary>Serial from the device's calibration namespace, e.g. "LQ-A41C".</summary>
    public required string Serial { get; set; }

    /// <summary>Protocol address, 1..999. Distinct from the serial: it is a setting.</summary>
    public int NodeId { get; set; }

    /// <summary>Board variant the firmware was built for. An image from a neighbouring revision is what turns a device into a brick.</summary>
    public string HardwareId { get; set; } = string.Empty;

    public string FirmwareVersion { get; set; } = string.Empty;

    /// <summary>Short git hash. Semver says what is compatible; the hash says which code it actually is.</summary>
    public string FirmwareHash { get; set; } = string.Empty;

    /// <summary>An image built from uncommitted changes. There is no commit to reproduce it from, so it must never be in the field.</summary>
    public bool FirmwareIsDirty { get; set; }

    /// <summary>dev, factory or field.</summary>
    public string BuildType { get; set; } = string.Empty;

    public int ProtocolVersion { get; set; }

    public int ConfigVersion { get; set; }

    /// <summary>
    /// The version an operator has decided this node should be on. Null means
    /// "whatever it is running is fine". Comparing it with
    /// <see cref="FirmwareVersion"/> is what produces the out-of-date list.
    /// </summary>
    public string? TargetFirmwareVersion { get; set; }

    public DateTime EnrolledAt { get; set; }

    public DateTime LastSeenAt { get; set; }

    public DeviceHealth? LastHealth { get; set; }

    /// <summary>Free text from the operator: where it is, which mast, who to call.</summary>
    public string? Notes { get; set; }

    /// <summary>Set when a node is retired, so it stops being reported as missing.</summary>
    public bool IsDecommissioned { get; set; }

    public bool IsOutOfDate =>
        !string.IsNullOrWhiteSpace(TargetFirmwareVersion) &&
        !string.Equals(TargetFirmwareVersion, FirmwareVersion, StringComparison.OrdinalIgnoreCase);
}
