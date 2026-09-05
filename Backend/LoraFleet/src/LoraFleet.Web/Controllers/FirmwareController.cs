using LoraFleet.DataAccess.Repositories;
using LoraFleet.Web.Models;
using LoraFleet.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers;

/// <summary>
/// The release shelf. An image is uploaded together with the manifest.json its
/// build produced, because under the Arduino framework a .bin carries no usable
/// identity of its own.
/// </summary>
public sealed class FirmwareController : Controller
{
    /// <summary>A field image is about 340 KB; anything near this is not one of ours.</summary>
    private const long MaxImageBytes = 8 * 1024 * 1024;

    private readonly IFirmwareRepository _firmware;
    private readonly IDeviceRepository _devices;
    private readonly IFirmwareService _service;
    private readonly ILogger<FirmwareController> _logger;

    public FirmwareController(
        IFirmwareRepository firmware,
        IDeviceRepository devices,
        IFirmwareService service,
        ILogger<FirmwareController> logger)
    {
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _service = service ?? throw new ArgumentNullException(nameof(service));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    [HttpGet("/firmware")]
    public async Task<IActionResult> Index(CancellationToken cancellationToken)
    {
        var releases = await _firmware.GetAllAsync(cancellationToken);
        var devices = await _devices.GetAllAsync(cancellationToken);

        var counts = devices
            .Where(device => !string.IsNullOrWhiteSpace(device.TargetFirmwareVersion))
            .GroupBy(device => device.TargetFirmwareVersion!, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(group => group.Key, group => group.Count(), StringComparer.OrdinalIgnoreCase);

        return View(new FirmwareListViewModel
        {
            Releases = releases,
            AssignmentCounts = counts,
        });
    }

    [HttpPost("/firmware/upload")]
    [ValidateAntiForgeryToken]
    [RequestSizeLimit(MaxImageBytes + (1024 * 1024))]
    public async Task<IActionResult> Upload(
        IFormFile? manifest,
        IFormFile? image,
        string? releaseNotes,
        CancellationToken cancellationToken)
    {
        if (manifest is null || manifest.Length == 0)
        {
            TempData["Error"] = "Select the manifest.json produced next to the image by the firmware build.";
            return RedirectToAction(nameof(Index));
        }

        if (image is { Length: > MaxImageBytes })
        {
            TempData["Error"] = $"That image is {image.Length / 1024} KB. A field build is around 340 KB, so this is not one of ours.";
            return RedirectToAction(nameof(Index));
        }

        try
        {
            await using var manifestStream = manifest.OpenReadStream();

            // Buffered, because the hash has to be computed and then the same
            // bytes stored, and a form stream cannot be rewound.
            await using var imageStream = image is null ? null : new MemoryStream();
            if (image is not null)
            {
                await image.CopyToAsync(imageStream!, cancellationToken);
                imageStream!.Position = 0;
            }

            var release = await _service.IngestAsync(
                manifestStream, imageStream, releaseNotes, cancellationToken);

            TempData["Message"] = release.IsDirty
                ? $"Stored {release.Id}, but it was built from uncommitted changes and can never be assigned."
                : $"Stored {release.Id}.";
        }
        catch (InvalidOperationException exception)
        {
            // Thrown for a manifest that does not parse, or an image that does
            // not match it. Both are the operator's problem to fix, not a fault.
            _logger.LogWarning(exception, "Firmware upload rejected");
            TempData["Error"] = exception.Message;
        }

        return RedirectToAction(nameof(Index));
    }

    [HttpPost("/firmware/{id}/delete")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Delete(string id, CancellationToken cancellationToken)
    {
        var release = await _firmware.GetByIdAsync(id, cancellationToken);
        if (release is null) return NotFound();

        var devices = await _devices.GetAllAsync(cancellationToken);
        var assigned = devices
            .Where(device => string.Equals(device.TargetFirmwareVersion, release.Version, StringComparison.OrdinalIgnoreCase))
            .Select(device => device.Serial)
            .ToList();

        // Refusing beats cascading. Silently clearing the assignment on a dozen
        // devices is a much bigger action than the operator asked for.
        if (assigned.Count > 0)
        {
            TempData["Error"] =
                $"{release.Id} is still assigned to {assigned.Count} device(s): {string.Join(", ", assigned)}. " +
                "Clear those assignments first.";
            return RedirectToAction(nameof(Index));
        }

        await _firmware.DeleteAsync(id, cancellationToken);
        TempData["Message"] = $"Deleted {id} and its image.";
        return RedirectToAction(nameof(Index));
    }
}
