using LoraFleet.DataAccess.Repositories;
using LoraFleet.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers;

/// <summary>
/// The whole fleet's log in one place, filtered. This is what a ticket gets
/// pasted from.
/// </summary>
public sealed class LogsController : Controller
{
    private const int DefaultLimit = 300;

    private readonly ILogRepository _logs;
    private readonly IDeviceRepository _devices;

    public LogsController(ILogRepository logs, IDeviceRepository devices)
    {
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
    }

    [HttpGet("/logs")]
    public async Task<IActionResult> Index(
        string? serial,
        byte? maxLevel,
        byte? tag,
        int? limit,
        CancellationToken cancellationToken)
    {
        var query = new LogQuery
        {
            DeviceSerial = string.IsNullOrWhiteSpace(serial) ? null : serial,
            MaxLevel = maxLevel,
            Tag = tag,
            Limit = limit ?? DefaultLimit,
        };

        var records = await _logs.QueryAsync(query, cancellationToken);
        var devices = await _devices.GetAllAsync(cancellationToken);

        return View(new LogsViewModel
        {
            Records = records,
            DeviceSerial = query.DeviceSerial,
            MaxLevel = maxLevel,
            Tag = tag,
            Limit = query.Limit,
            KnownSerials = devices.Select(device => device.Serial).Order(StringComparer.OrdinalIgnoreCase).ToList(),
        });
    }
}
