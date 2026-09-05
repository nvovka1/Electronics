namespace LoraFleet.Domain.Firmware;

/// <summary>
/// One built image, with everything needed to decide whether it may be written
/// to a given board. A .bin on its own is anonymous: under the Arduino
/// framework the descriptor baked into the image describes the framework
/// builder rather than the project, so this record is the only identity it has.
/// </summary>
public sealed class FirmwareRelease
{
    /// <summary>"1.0.0+59b0d70-field" - version, hash and image type together, because all three vary independently.</summary>
    public string Id { get; set; } = string.Empty;

    public required string Version { get; set; }

    /// <summary>Semver says what is compatible; the hash says which code it actually is.</summary>
    public string GitHash { get; set; } = string.Empty;

    /// <summary>Built from uncommitted changes. There is no commit to reproduce such an image from, so it is never deployable.</summary>
    public bool IsDirty { get; set; }

    /// <summary>dev, factory or field.</summary>
    public string BuildType { get; set; } = string.Empty;

    /// <summary>
    /// Board variant. Two revisions with different pin assignments and the same
    /// connector are how an image from a neighbouring board makes smoke.
    /// </summary>
    public string HardwareId { get; set; } = string.Empty;

    public int ProtocolVersion { get; set; }

    public long SizeBytes { get; set; }

    /// <summary>Of the whole image. A CRC-16 is not enough for a third of a megabyte.</summary>
    public string Sha256 { get; set; } = string.Empty;

    public DateTime BuildUtc { get; set; }

    public DateTime UploadedAt { get; set; }

    /// <summary>GridFS id of the .bin, when one was uploaded alongside the manifest.</summary>
    public string? BinaryFileId { get; set; }

    /// <summary>What changed, what broke, what somebody upgrading has to do.</summary>
    public string? ReleaseNotes { get; set; }

    public bool HasBinary => !string.IsNullOrEmpty(BinaryFileId);

    /// <summary>A dirty image is never deployable, however much an operator wants it to be.</summary>
    public bool IsDeployable => !IsDirty && HasBinary;

    public string DisplayName => $"{Version}+{GitHash}{(IsDirty ? "-dirty" : string.Empty)} {BuildType}";

    public static string BuildId(string version, string gitHash, string buildType) =>
        $"{version}+{gitHash}-{buildType}";
}
