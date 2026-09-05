using LoraFleet.DataAccess.Repositories;
using LoraFleet.Domain.Devices;
using LoraFleet.Web.Infrastructure;
using LoraFleet.Web.Models;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace LoraFleet.Web.Controllers;

/// <summary>The operator's view of the fleet.</summary>
public sealed class DevicesController : Controller
{
    private const int RecentLogCount = 100;

    private readonly IDeviceRepository _devices;
    private readonly ILogRepository _logs;
    private readonly IFirmwareRepository _firmware;
    private readonly FleetOptions _options;
    private readonly TimeProvider _clock;

    public DevicesController(
        IDeviceRepository devices,
        ILogRepository logs,
        IFirmwareRepository firmware,
        IOptions<FleetOptions> options,
        TimeProvider clock)
    {
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    [HttpGet("/")]
    [HttpGet("/devices")]
    public async Task<IActionResult> Index(CancellationToken cancellationToken)
    {
        var now = _clock.GetUtcNow().UtcDateTime;
        var devices = await _devices.GetAllAsync(cancellationToken);

        var rows = devices
            .Select(device => new DeviceRowViewModel
            {
                Device = device,
                Status = DeviceStatusPolicy.Evaluate(device, _options.ExpectedReportPeriod, now),
                Silence = now - device.LastSeenAt,
            })
            // Anything wrong floats to the top. In a list of forty, the two that
            // need a decision must not be somewhere in the middle.
            .OrderByDescending(row => row.NeedsAttention)
            .ThenBy(row => row.Device.Serial, StringComparer.OrdinalIgnoreCase)
            .ToList();

        return View(new DeviceListViewModel { Devices = rows });
    }

    [HttpGet("/devices/{serial}")]
    public async Task<IActionResult> Details(string serial, CancellationToken cancellationToken)
    {
        var device = await _devices.GetBySerialAsync(serial, cancellationToken);
        if (device is null) return NotFound();

        var now = _clock.GetUtcNow().UtcDateTime;

        var logs = await _logs.QueryAsync(
            new LogQuery { DeviceSerial = serial, Limit = RecentLogCount }, cancellationToken);

        var releases = await _firmware.GetAllAsync(cancellationToken);

        return View(new DeviceDetailViewModel
        {
            Device = device,
            Status = DeviceStatusPolicy.Evaluate(device, _options.ExpectedReportPeriod, now),
            Silence = now - device.LastSeenAt,
            RecentLogs = logs,
            AssignableReleases = releases
                .Where(release => release.IsDeployable)
                .Where(release =>
                    string.IsNullOrEmpty(release.HardwareId) ||
                    string.IsNullOrEmpty(device.HardwareId) ||
                    string.Equals(release.HardwareId, device.HardwareId, StringComparison.OrdinalIgnoreCase))
                .ToList(),
        });
    }

    [HttpPost("/devices/{serial}/target-firmware")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> SetTargetFirmware(
        string serial, string? targetVersion, CancellationToken cancellationToken)
    {
        // An empty selection clears the assignment rather than storing a blank
        // string, so "no opinion" and "assigned to nothing" stay one state.
        var normalised = string.IsNullOrWhiteSpace(targetVersion) ? null : targetVersion.Trim();

        await _devices.SetTargetFirmwareAsync(serial, normalised, cancellationToken);

        TempData["Message"] = normalised is null
            ? $"{serial}: target version cleared."
            : $"{serial}: target version set to {normalised}.";

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost("/devices/{serial}/notes")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> SetNotes(
        string serial, string? notes, CancellationToken cancellationToken)
    {
        await _devices.SetNotesAsync(serial, string.IsNullOrWhiteSpace(notes) ? null : notes.Trim(), cancellationToken);
        TempData["Message"] = $"{serial}: notes saved.";
        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost("/devices/{serial}/decommission")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> SetDecommissioned(
        string serial, bool isDecommissioned, CancellationToken cancellationToken)
    {
        await _devices.SetDecommissionedAsync(serial, isDecommissioned, cancellationToken);

        TempData["Message"] = isDecommissioned
            ? $"{serial}: retired. It will no longer be reported as missing."
            : $"{serial}: back in service.";

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost("/devices/{serial}/delete")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Delete(string serial, CancellationToken cancellationToken)
    {
        // Logs go too. Leaving them behind would mean a node that re-enrols with
        // the same serial inherits a stranger's history.
        await _logs.DeleteForDeviceAsync(serial, cancellationToken);
        await _devices.DeleteAsync(serial, cancellationToken);

        TempData["Message"] = $"{serial}: deleted along with its log records. It will re-enrol on its next check-in.";
        return RedirectToAction(nameof(Index));
    }
}
