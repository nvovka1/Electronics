using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;
using Initiator.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>
/// Where a node reports on itself. Enrolment is implicit: the first report from
/// a serial creates the device.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/health-reports")]
[Produces("application/json")]
public sealed class HealthReportsController : DeviceApiControllerBase
{
    private readonly IDeviceCheckInService _checkIns;

    public HealthReportsController(IDeviceCheckInService checkIns)
    {
        _checkIns = checkIns ?? throw new ArgumentNullException(nameof(checkIns));
    }

    [HttpPost]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Create(
        string serial, [FromBody] CheckInRequest request, CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        var device = await _checkIns.RecordCheckInAsync(serial, request, cancellationToken);

        // 202 rather than 201: the node is not creating a resource it will go
        // and fetch, it is telling us something.
        return Accepted(new
        {
            device.Serial,
            device.LastSeenAt,

            // Handed straight back so the node can see whether this service
            // agrees with it about its own state. A disagreement means a state
            // event went missing, and the node can re-send rather than wait for
            // the next transition to correct the record.
            state = (byte)device.CurrentState,
        });
    }
}
