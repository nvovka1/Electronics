using LoraFleet.DataAccess.Repositories;
using LoraFleet.Web.Infrastructure;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers.Api;

/// <summary>
/// Serves the image itself. Separate from the manifest on purpose: a client
/// decides from the manifest whether to spend the bandwidth at all, and only
/// then asks for the bytes.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/firmware-releases/{releaseId}/image")]
public sealed class FirmwareImagesController : ControllerBase
{
    private readonly IFirmwareRepository _firmware;

    public FirmwareImagesController(IFirmwareRepository firmware)
    {
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
    }

    [HttpGet]
    [ProducesResponseType(StatusCodes.Status200OK)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<IActionResult> Get(string releaseId, CancellationToken cancellationToken)
    {
        var release = await _firmware.GetByIdAsync(releaseId, cancellationToken);
        if (release?.BinaryFileId is null)
            return NotFound(new ProblemDetails { Title = $"No image stored for release {releaseId}" });

        var stream = await _firmware.OpenBinaryAsync(release.BinaryFileId, cancellationToken);
        if (stream is null)
            return NotFound(new ProblemDetails { Title = "The release record exists but its image is gone" });

        // The hash travels in a header as well as in the manifest, so a client
        // that streams straight to flash can verify without holding the whole
        // image in memory first.
        Response.Headers["X-Firmware-Sha256"] = release.Sha256;
        Response.Headers["X-Firmware-Version"] = release.Version;
        Response.Headers["X-Firmware-Hardware-Id"] = release.HardwareId;

        // enableRangeProcessing lets an interrupted download resume rather than
        // start again - the difference between a retry and a wasted transfer on
        // a slow link.
        return File(stream, "application/octet-stream", $"{release.Id}.bin", enableRangeProcessing: true);
    }
}
