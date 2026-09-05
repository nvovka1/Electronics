using LoraFleet.Web.Contracts;
using LoraFleet.Web.Infrastructure;
using LoraFleet.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers.Api;

/// <summary>
/// Where a node reports on itself. Enrolment is implicit: the first report from
/// a serial creates the device.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/health-reports")]
[Produces("application/json")]
public sealed class HealthReportsController : ControllerBase
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
        if (!IsPlausibleSerial(serial))
            return BadRequest(new ProblemDetails { Title = "Invalid serial" });

        var device = await _checkIns.RecordCheckInAsync(serial, request, cancellationToken);

        // 202 rather than 201: the node is not creating a resource it will go
        // and fetch, it is telling us something. A device on a battery should
        // not be made to parse a body it has no use for.
        return Accepted(new
        {
            device.Serial,
            device.LastSeenAt,
            // Handed straight back so a node learns it is out of date on the
            // same round trip, without a second request.
            targetFirmwareVersion = device.TargetFirmwareVersion,
            updateAvailable = device.IsOutOfDate,
        });
    }

    /// <summary>
    /// The serial ends up in a database key and in a URL, so it is bounded here
    /// rather than trusted. Devices derive it from the chip MAC when they have
    /// never been provisioned, so the format is not guaranteed to be pretty -
    /// only sane.
    /// </summary>
    private static bool IsPlausibleSerial(string serial) =>
        !string.IsNullOrWhiteSpace(serial) &&
        serial.Length <= 32 &&
        serial.All(character => char.IsAsciiLetterOrDigit(character) || character is '-' or '_');
}
