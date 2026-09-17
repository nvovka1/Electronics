using Initiator.DataAccess.Repositories;
using Initiator.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers;

public sealed class LogsController : Controller
{
    private const int DefaultLimit = 200;

    private readonly ILogRepository _logs;
    private readonly IDeviceRepository _devices;

    public LogsController(ILogRepository logs, IDeviceRepository devices)
    {
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
    }

    public async Task<IActionResult> Index(
        string? serial,
        byte? maxLevel,
        byte? tag,
        int limit = DefaultLimit,
        CancellationToken cancellationToken = default)
    {
        var query = new LogQuery
        {
            DeviceSerial = serial,
            MaxLevel = maxLevel,
            Tag = tag,
            Limit = limit,
        };

        var records = await _logs.QueryAsync(query, cancellationToken);
        var total = await _logs.CountAsync(query, cancellationToken);
        var devices = await _devices.GetAllAsync(cancellationToken);

        return View(new LogListViewModel
        {
            Records = records,
            KnownSerials = [.. devices.Select(device => device.Serial).Order()],
            Serial = serial,
            MaxLevel = maxLevel,
            Tag = tag,
            Limit = limit,
            TotalMatching = total,
        });
    }
}
