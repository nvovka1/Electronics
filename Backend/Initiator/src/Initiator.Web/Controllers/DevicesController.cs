using Initiator.DataAccess.Repositories;
using Initiator.Domain.Devices;
using Initiator.Domain.States;
using Initiator.Web.Infrastructure;
using Initiator.Web.Models;
using Initiator.Web.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Initiator.Web.Controllers;

public sealed class DevicesController : Controller
{
    private const int RecentEventsOnDetails = 25;
    private const int RecentCommandsOnDetails = 15;
    private const int RecentLogsOnDetails = 40;

    private readonly IDeviceRepository _devices;
    private readonly IStateEventRepository _events;
    private readonly ICommandRepository _commands;
    private readonly ILogRepository _logs;
    private readonly ICommandDispatchService _dispatch;
    private readonly InitiatorOptions _options;
    private readonly TimeProvider _clock;

    public DevicesController(
        IDeviceRepository devices,
        IStateEventRepository events,
        ICommandRepository commands,
        ILogRepository logs,
        ICommandDispatchService dispatch,
        IOptions<InitiatorOptions> options,
        TimeProvider clock)
    {
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _events = events ?? throw new ArgumentNullException(nameof(events));
        _commands = commands ?? throw new ArgumentNullException(nameof(commands));
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _dispatch = dispatch ?? throw new ArgumentNullException(nameof(dispatch));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    public async Task<IActionResult> Index(CancellationToken cancellationToken)
    {
        var now = _clock.GetUtcNow().UtcDateTime;
        var devices = await _devices.GetAllAsync(cancellationToken);

        var rows = devices
            .Select(device => new DeviceRow
            {
                Device = device,
                Status = DeviceStatusPolicy.Evaluate(device, _options.ExpectedReportPeriod, now),
                Silence = now - device.LastSeenAt,
            })
            // Anything not in SAFE floats to the top, then anything unwell, then
            // the rest by how recently it spoke. A node sitting in ARMED is not
            // a fault, but it is not something to scroll for either.
            .OrderByDescending(row => row.IsLive)
            .ThenByDescending(row => row.NeedsAttention)
            .ThenByDescending(row => row.Device.LastSeenAt)
            .ToArray();

        return View(new DeviceListViewModel { Devices = rows });
    }

    [HttpGet("Devices/Details/{serial}")]
    public async Task<IActionResult> Details(string serial, CancellationToken cancellationToken)
    {
        var device = await _devices.GetBySerialAsync(serial, cancellationToken);
        if (device is null) return NotFound();

        var now = _clock.GetUtcNow().UtcDateTime;

        var events = await _events.QueryAsync(serial, RecentEventsOnDetails, cancellationToken);
        var commands = await _commands.QueryAsync(serial, RecentCommandsOnDetails, cancellationToken);
        var logs = await _logs.QueryAsync(
            new LogQuery { DeviceSerial = serial, Limit = RecentLogsOnDetails }, cancellationToken);

        return View(new DeviceDetailsViewModel
        {
            Device = device,
            Status = DeviceStatusPolicy.Evaluate(device, _options.ExpectedReportPeriod, now),
            AllowedCommands = NodeStateMachine.AllowedFrom(device.CurrentState),
            RecentEvents = events,
            RecentCommands = commands,
            RecentLogs = logs,
            AutoArmInSeconds = SecondsUntilAutoArm(device, now),
        });
    }

    [HttpPost]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Queue(
        string serial, CommandType command, string? note, CancellationToken cancellationToken)
    {
        var outcome = await _dispatch.QueueAsync(serial, command, note, cancellationToken);

        if (outcome.Queued)
        {
            TempData["Message"] =
                $"{command.ToString().ToUpperInvariant()} queued for {serial}. " +
                "The node will collect it on its next poll.";
        }
        else
        {
            TempData["Error"] = outcome.Problem;
        }

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> CancelCommand(
        string serial, string commandId, CancellationToken cancellationToken)
    {
        var cancelled = await _dispatch.CancelAsync(commandId, cancellationToken);

        TempData[cancelled ? "Message" : "Error"] = cancelled
            ? "Command withdrawn before the node collected it."
            : "Too late to withdraw — the node has already taken that command.";

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Notes(
        string serial, string? notes, CancellationToken cancellationToken)
    {
        await _devices.SetNotesAsync(serial, notes, cancellationToken);
        TempData["Message"] = "Notes saved.";

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Decommission(
        string serial, bool isDecommissioned, CancellationToken cancellationToken)
    {
        await _devices.SetDecommissionedAsync(serial, isDecommissioned, cancellationToken);

        TempData["Message"] = isDecommissioned
            ? $"{serial} retired. It will stop being reported as missing."
            : $"{serial} returned to service.";

        return RedirectToAction(nameof(Details), new { serial });
    }

    [HttpPost]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Delete(string serial, CancellationToken cancellationToken)
    {
        // Everything for this serial, not just the device row: leaving its
        // history behind would make the next node to enrol with the same serial
        // inherit somebody else's record.
        await _commands.DeleteForDeviceAsync(serial, cancellationToken);
        await _events.DeleteForDeviceAsync(serial, cancellationToken);
        await _logs.DeleteForDeviceAsync(serial, cancellationToken);
        await _devices.DeleteAsync(serial, cancellationToken);

        TempData["Message"] =
            $"{serial} deleted. It will re-enrol by itself if it checks in again.";

        return RedirectToAction(nameof(Index));
    }

    /// <summary>
    /// An estimate, deliberately. The node runs the only countdown that decides
    /// anything; this is arithmetic over the last thing it told us.
    /// </summary>
    private static int? SecondsUntilAutoArm(Device device, DateTime now)
    {
        var dueAt = device.AutoArmDueAt;
        if (dueAt is null) return null;

        var remaining = (dueAt.Value - now).TotalSeconds;
        return remaining <= 0 ? 0 : (int)Math.Round(remaining);
    }
}
