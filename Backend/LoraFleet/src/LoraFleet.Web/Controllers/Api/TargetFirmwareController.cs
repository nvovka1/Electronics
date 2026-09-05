using LoraFleet.DataAccess.Repositories;
using LoraFleet.Web.Contracts;
using LoraFleet.Web.Infrastructure;
using LoraFleet.Web.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace LoraFleet.Web.Controllers.Api;

/// <summary>
/// What this node should be running. 204 means "nothing to do" - the common
/// answer, and the cheapest one for a device on a battery to handle.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/target-firmware")]
[Produces("application/json")]
public sealed class TargetFirmwareController : ControllerBase
{
    private readonly IDeviceRepository _devices;
    private readonly IFirmwareService _firmware;
    private readonly FleetOptions _options;

    public TargetFirmwareController(
        IDeviceRepository devices, IFirmwareService firmware, IOptions<FleetOptions> options)
    {
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
    }

    [HttpGet]
    [ProducesResponseType(typeof(UpdateManifestResponse), StatusCodes.Status200OK)]
    [ProducesResponseType(StatusCodes.Status204NoContent)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<IActionResult> Get(string serial, CancellationToken cancellationToken)
    {
        var device = await _devices.GetBySerialAsync(serial, cancellationToken);
        if (device is null)
            return NotFound(new ProblemDetails { Title = $"No device enrolled with serial {serial}" });

        var release = await _firmware.ResolveTargetAsync(device, cancellationToken);
        if (release is null) return NoContent();

        return Ok(new UpdateManifestResponse
        {
            Version = release.Version,
            GitHash = release.GitHash,
            BuildType = release.BuildType,
            HardwareId = release.HardwareId,
            ProtocolVersion = release.ProtocolVersion,
            SizeBytes = release.SizeBytes,
            Sha256 = release.Sha256,
            BuildUtc = release.BuildUtc,
            DownloadUrl = BuildDownloadUrl(release.Id),
            ReleaseNotes = release.ReleaseNotes,
        });
    }

    /// <summary>
    /// Behind a reverse proxy the request's own host is the container's, which
    /// a device on the far side cannot reach. The configured public base URL
    /// wins whenever it is set.
    /// </summary>
    private string BuildDownloadUrl(string releaseId)
    {
        var path = $"/api/v1/firmware-releases/{Uri.EscapeDataString(releaseId)}/image";

        if (!string.IsNullOrWhiteSpace(_options.PublicBaseUrl))
            return $"{_options.PublicBaseUrl.TrimEnd('/')}{path}";

        return $"{Request.Scheme}://{Request.Host}{path}";
    }
}
