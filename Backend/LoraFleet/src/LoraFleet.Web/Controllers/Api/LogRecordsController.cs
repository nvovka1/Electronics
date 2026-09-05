using LoraFleet.Web.Contracts;
using LoraFleet.Web.Infrastructure;
using LoraFleet.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers.Api;

/// <summary>
/// Where a node hands over a slice of its ring log. This is what replaces the
/// drive out: the records from before a fall are the valuable part, and there
/// is no other way to get them off a box under a lock.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/log-records")]
[Produces("application/json")]
public sealed class LogRecordsController : ControllerBase
{
    private readonly IDeviceCheckInService _checkIns;

    public LogRecordsController(IDeviceCheckInService checkIns)
    {
        _checkIns = checkIns ?? throw new ArgumentNullException(nameof(checkIns));
    }

    [HttpPost]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Create(
        string serial, [FromBody] LogBatchRequest request, CancellationToken cancellationToken)
    {
        var stored = await _checkIns.RecordLogsAsync(serial, request, cancellationToken);

        return Accepted(new { serial, stored });
    }
}
