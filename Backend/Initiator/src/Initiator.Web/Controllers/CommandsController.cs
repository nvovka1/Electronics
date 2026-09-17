using Initiator.DataAccess.Repositories;
using Initiator.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers;

/// <summary>
/// The queue and what became of it. Commands from the handheld controller never
/// appear here — they go straight over the radio — so this page is deliberately
/// only half the story, and the state history is the other half.
/// </summary>
public sealed class CommandsController : Controller
{
    private const int DefaultLimit = 100;

    private readonly ICommandRepository _commands;
    private readonly IDeviceRepository _devices;

    public CommandsController(ICommandRepository commands, IDeviceRepository devices)
    {
        _commands = commands ?? throw new ArgumentNullException(nameof(commands));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
    }

    public async Task<IActionResult> Index(
        string? serial, int limit = DefaultLimit, CancellationToken cancellationToken = default)
    {
        var commands = await _commands.QueryAsync(serial, limit, cancellationToken);
        var devices = await _devices.GetAllAsync(cancellationToken);

        return View(new CommandListViewModel
        {
            Commands = commands,
            KnownSerials = [.. devices.Select(device => device.Serial).Order()],
            Serial = serial,
            Limit = limit,
        });
    }
}
