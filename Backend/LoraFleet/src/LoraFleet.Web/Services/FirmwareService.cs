using System.Security.Cryptography;
using System.Text.Json;
using LoraFleet.DataAccess.Repositories;
using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Firmware;

namespace LoraFleet.Web.Services;

public interface IFirmwareService
{
    /// <summary>
    /// Takes a manifest.json produced by the firmware build, plus optionally the
    /// image itself, and records the release.
    /// </summary>
    Task<FirmwareRelease> IngestAsync(
        Stream manifestJson,
        Stream? binary,
        string? releaseNotes,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// What this device should be running: its explicit assignment if the
    /// operator made one, otherwise nothing. Returns null when the node is
    /// already on the assigned version, or when the assignment names a release
    /// that is not deployable to this board.
    /// </summary>
    Task<FirmwareRelease?> ResolveTargetAsync(
        Device device, CancellationToken cancellationToken = default);
}

public sealed class FirmwareService : IFirmwareService
{
    // The build script writes snake_case keys (git_hash, build_utc, hw_id), so
    // the naming policy has to be told - case-insensitivity alone would not
    // bridge git_hash to GitHash and every such field would silently come back
    // empty.
    private static readonly JsonSerializerOptions ManifestJson = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true,
    };

    private readonly IFirmwareRepository _firmware;
    private readonly TimeProvider _clock;
    private readonly ILogger<FirmwareService> _logger;

    public FirmwareService(
        IFirmwareRepository firmware, TimeProvider clock, ILogger<FirmwareService> logger)
    {
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task<FirmwareRelease> IngestAsync(
        Stream manifestJson,
        Stream? binary,
        string? releaseNotes,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(manifestJson);

        var manifest = await JsonSerializer.DeserializeAsync<BuildManifest>(
            manifestJson, ManifestJson, cancellationToken)
            ?? throw new InvalidOperationException("The manifest is empty or not valid JSON.");

        if (string.IsNullOrWhiteSpace(manifest.Version))
            throw new InvalidOperationException("The manifest has no version.");

        var release = new FirmwareRelease
        {
            Id = FirmwareRelease.BuildId(manifest.Version, manifest.GitHash, manifest.BuildType),
            Version = manifest.Version,
            GitHash = manifest.GitHash,
            IsDirty = manifest.Dirty,
            BuildType = manifest.BuildType,
            HardwareId = manifest.HwId,
            ProtocolVersion = manifest.ProtoVersion,
            SizeBytes = manifest.SizeBytes,
            Sha256 = manifest.Sha256,
            BuildUtc = manifest.BuildUtc,
            UploadedAt = _clock.GetUtcNow().UtcDateTime,
            ReleaseNotes = releaseNotes,
        };

        if (binary is not null)
        {
            // Hash what was actually uploaded and compare it with what the
            // manifest claims. A file that does not match its manifest is the
            // one thing that must never reach a board, and this is the only
            // moment we can catch it cheaply.
            var (actualSha, actualSize) = await HashAsync(binary, cancellationToken);

            if (!string.IsNullOrEmpty(release.Sha256) &&
                !string.Equals(actualSha, release.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException(
                    $"The uploaded image does not match its manifest. " +
                    $"Manifest says {release.Sha256}, the file hashes to {actualSha}.");
            }

            if (release.SizeBytes != 0 && release.SizeBytes != actualSize)
            {
                throw new InvalidOperationException(
                    $"The uploaded image is {actualSize} bytes, but the manifest says {release.SizeBytes}.");
            }

            release.Sha256 = actualSha;
            release.SizeBytes = actualSize;

            binary.Position = 0;
            release.BinaryFileId = await _firmware.StoreBinaryAsync(
                $"{release.Id}.bin", binary, cancellationToken);
        }

        await _firmware.UpsertAsync(release, cancellationToken);

        if (release.IsDirty)
        {
            _logger.LogWarning(
                "Stored a dirty release {Id}. It is recorded for reference but can never be assigned.",
                release.Id);
        }

        return release;
    }

    public async Task<FirmwareRelease?> ResolveTargetAsync(
        Device device, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(device);

        if (string.IsNullOrWhiteSpace(device.TargetFirmwareVersion)) return null;

        // Already there. Answering with a manifest anyway would make a client
        // re-download an image it is currently running.
        if (string.Equals(device.TargetFirmwareVersion, device.FirmwareVersion, StringComparison.OrdinalIgnoreCase))
            return null;

        var candidates = await _firmware.GetAllAsync(cancellationToken);

        var match = candidates
            .Where(release =>
                string.Equals(release.Version, device.TargetFirmwareVersion, StringComparison.OrdinalIgnoreCase) &&
                release.IsDeployable)
            // Hardware id has to match, or the image is for a board with
            // different pins. An unknown hardware id on either side is treated
            // as a match so a node that has never reported one is not stranded.
            .Where(release =>
                string.IsNullOrEmpty(release.HardwareId) ||
                string.IsNullOrEmpty(device.HardwareId) ||
                string.Equals(release.HardwareId, device.HardwareId, StringComparison.OrdinalIgnoreCase))
            .OrderByDescending(release => release.BuildUtc)
            .FirstOrDefault();

        if (match is null)
        {
            _logger.LogWarning(
                "Device {Serial} is assigned {Version}, but no deployable release matches it for hardware {HardwareId}",
                device.Serial, device.TargetFirmwareVersion, device.HardwareId);
        }

        return match;
    }

    private static async Task<(string Sha256, long Size)> HashAsync(
        Stream content, CancellationToken cancellationToken)
    {
        if (content.CanSeek) content.Position = 0;

        using var sha = SHA256.Create();
        var hash = await sha.ComputeHashAsync(content, cancellationToken);
        var size = content.CanSeek ? content.Length : 0;

        return (Convert.ToHexString(hash).ToLowerInvariant(), size);
    }

    /// <summary>
    /// The shape scripts/release_manifest.py writes next to each built image.
    /// Deserialised into its own type rather than straight into the domain
    /// entity, so a change to the build script cannot silently reshape what is
    /// stored.
    /// </summary>
    private sealed class BuildManifest
    {
        public string Name { get; set; } = string.Empty;
        public string Version { get; set; } = string.Empty;
        public string GitHash { get; set; } = string.Empty;
        public bool Dirty { get; set; }
        public DateTime BuildUtc { get; set; }
        public string BuildType { get; set; } = string.Empty;
        public int ProtoVersion { get; set; }
        public string HwId { get; set; } = string.Empty;
        public string Board { get; set; } = string.Empty;
        public long SizeBytes { get; set; }
        public string Sha256 { get; set; } = string.Empty;
    }
}
