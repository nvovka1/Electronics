using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;
using Initiator.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>
/// Every transition a node makes, accepted or refused. This is the record of
/// what actually happened, as opposed to what was asked for.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/state-events")]
[Produces("application/json")]
public sealed class StateEventsController : DeviceApiControllerBase
{
    /// <summary>
    /// A node flushing a backlog after an outage is the normal case, but a batch
    /// past this size is a runaway rather than a backlog: the ring log on the
    /// device cannot hold more than a few hundred.
    /// </summary>
    private const int MaxEventsPerBatch = 500;

    private readonly IStateEventIngestService _ingest;

    public StateEventsController(IStateEventIngestService ingest)
    {
        _ingest = ingest ?? throw new ArgumentNullException(nameof(ingest));
    }

    [HttpPost]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Create(
        string serial, [FromBody] StateEventBatchRequest request, CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        if (request.Events.Count > MaxEventsPerBatch)
        {
            return BadRequest(new ProblemDetails
            {
                Title = "Batch too large",
                Detail = $"Send at most {MaxEventsPerBatch} events per request.",
                Status = StatusCodes.Status400BadRequest,
            });
        }

        var outcome = await _ingest.RecordAsync(serial, request, cancellationToken);

        // The count is echoed because the node uses it to decide what it may
        // drop from its buffer. Reporting more than was stored would lose
        // exactly the events this endpoint could not understand.
        return Accepted(new { stored = outcome.Stored, discarded = outcome.Discarded });
    }
}
