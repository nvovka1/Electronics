using Initiator.DataAccess.Repositories;
using Initiator.Domain.Logs;
using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>Records out of a node's ring log.</summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/log-records")]
[Produces("application/json")]
public sealed class LogRecordsController : DeviceApiControllerBase
{
    private const int MaxRecordsPerBatch = 500;

    private readonly ILogRepository _logs;
    private readonly TimeProvider _clock;

    public LogRecordsController(ILogRepository logs, TimeProvider clock)
    {
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    [HttpPost]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Create(
        string serial, [FromBody] LogBatchRequest request, CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        if (request.Records.Count > MaxRecordsPerBatch)
        {
            return BadRequest(new ProblemDetails
            {
                Title = "Batch too large",
                Detail = $"Send at most {MaxRecordsPerBatch} records per request.",
                Status = StatusCodes.Status400BadRequest,
            });
        }

        var now = _clock.GetUtcNow().UtcDateTime;

        var records = request.Records
            .Select(record => new LogRecord
            {
                DeviceSerial = serial,
                TimestampMs = record.TimestampMs,
                ReceivedAt = now,
                Level = record.Level,
                Tag = record.Tag,
                Code = record.Code,
                Arg = record.Arg,
            })
            .ToArray();

        await _logs.AppendAsync(records, cancellationToken);

        return Accepted(new { stored = records.Length });
    }
}
