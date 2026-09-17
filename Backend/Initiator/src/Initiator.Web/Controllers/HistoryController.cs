using Initiator.DataAccess.Repositories;
using Initiator.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers;

/// <summary>
/// Every transition every node has made, from both sources. This is the record
/// of what happened, as against the queue's record of what was asked for.
/// </summary>
public sealed class HistoryController : Controller
{
    private const int DefaultLimit = 200;

    private readonly IStateEventRepository _events;
    private readonly IDeviceRepository _devices;

    public HistoryController(IStateEventRepository events, IDeviceRepository devices)
    {
        _events = events ?? throw new ArgumentNullException(nameof(events));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
    }

    public async Task<IActionResult> Index(
        string? serial, int limit = DefaultLimit, CancellationToken cancellationToken = default)
    {
        var events = await _events.QueryAsync(serial, limit, cancellationToken);
        var devices = await _devices.GetAllAsync(cancellationToken);

        return View(new StateHistoryViewModel
        {
            Events = events,
            KnownSerials = [.. devices.Select(device => device.Serial).Order()],
            Serial = serial,
            Limit = limit,
        });
    }
}
