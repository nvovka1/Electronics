namespace LoraFleet.Web.Contracts;

/// <summary>
/// What a node should be running, and everything it needs to check an image
/// before writing a single byte of it.
/// </summary>
/// <remarks>
/// The firmware in this fleet has no OTA client - over LoRa a third of a
/// megabyte is close to forty minutes of transmit, which is a battery discharge
/// rather than an update - so today this is read by an operator's tooling and
/// by the dashboard. The fields are the ones an OTA client would need on the
/// day one exists: hardware id so an image from a neighbouring board revision
/// is refused, size and SHA-256 so a truncated download is caught before the
/// slot is switched.
/// </remarks>
public sealed class UpdateManifestResponse
{
    public required string Version { get; init; }

    public string GitHash { get; init; } = string.Empty;

    public string BuildType { get; init; } = string.Empty;

    /// <summary>Board variant. An image from a different revision has different pin assignments.</summary>
    public string HardwareId { get; init; } = string.Empty;

    public int ProtocolVersion { get; init; }

    public long SizeBytes { get; init; }

    /// <summary>Of the whole image, hex. A CRC-16 is not enough for this much data.</summary>
    public string Sha256 { get; init; } = string.Empty;

    public DateTime BuildUtc { get; init; }

    /// <summary>Absolute URL of the image itself.</summary>
    public string DownloadUrl { get; init; } = string.Empty;

    public string? ReleaseNotes { get; init; }
}
