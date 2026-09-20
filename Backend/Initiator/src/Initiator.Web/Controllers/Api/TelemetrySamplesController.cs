using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;
using Initiator.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>
/// Where an aircraft's telemetry logger sends its rows. Enrolment is implicit:
/// the first batch from a serial creates the flight, and the aircraft is the set
/// of flights carrying that serial.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/aircraft/{serial}/telemetry-samples")]
[Produces("application/json")]
public sealed class TelemetrySamplesController : DeviceApiControllerBase
{
    /// <summary>
    /// Rows per request. The board sends about 25; this is a bound on what a
    /// single request can make this service do, not a target.
    /// </summary>
    private const int MaxSamplesPerBatch = 1000;

    private readonly ITelemetryIngestService _ingest;

    public TelemetrySamplesController(ITelemetryIngestService ingest)
    {
        _ingest = ingest ?? throw new ArgumentNullException(nameof(ingest));
    }

    [HttpPost]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Create(
        string serial, [FromBody] TelemetryBatchRequest request, CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        if (request.FlightId <= 0)
        {
            return Problem("A flight id is the board's boot count, which starts at 1.", 400);
        }

        if (request.Samples.Count == 0)
        {
            return Problem("A batch with no samples has nothing to store.", 400);
        }

        if (request.Samples.Count > MaxSamplesPerBatch)
        {
            return Problem($"A batch carries at most {MaxSamplesPerBatch} samples.", 400);
        }

        var result = await _ingest.IngestAsync(serial, request, cancellationToken);

        // 202 rather than 201: the board is not creating a resource it will go
        // and fetch, it is telling us what happened.
        //
        // nextIndex is the part that matters. The board sets its cursor from it
        // rather than from its own arithmetic, so a batch that succeeded but
        // whose response was lost costs one repeated request and nothing else.
        return Accepted(new
        {
            result.FlightId,
            result.NextIndex,
            result.Stored,
        });
    }

    private IActionResult Problem(string detail, int status) =>
        StatusCode(status, new ProblemDetails
        {
            Title = "Invalid telemetry batch",
            Detail = detail,
            Status = status,
        });
}
