using Initiator.Web.Contracts;

namespace Initiator.Web.Services;

/// <summary>
/// What this service tells the board after storing a batch.
/// </summary>
/// <param name="FlightId">Echoed back, so a response is legible on its own.</param>
/// <param name="NextIndex">
/// The index this service wants next, read from what it actually holds rather
/// than from what the board said it sent. The board sets its cursor from this,
/// which is what makes a lost acknowledgement cost nothing and a genuine gap
/// repair itself.
/// </param>
/// <param name="Stored">
/// How many rows in the batch were new. A batch that is entirely duplicates
/// stores none and is still a success — that is the retry path working.
/// </param>
public readonly record struct TelemetryIngestResult(int FlightId, int NextIndex, int Stored);

public interface ITelemetryIngestService
{
    Task<TelemetryIngestResult> IngestAsync(
        string serial, TelemetryBatchRequest request, CancellationToken cancellationToken = default);
}
