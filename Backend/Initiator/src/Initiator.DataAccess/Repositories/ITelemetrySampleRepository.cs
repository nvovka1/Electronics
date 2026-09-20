using Initiator.Domain.Telemetry;

namespace Initiator.DataAccess.Repositories;

public interface ITelemetrySampleRepository
{
    /// <summary>
    /// Stores a batch, discarding rows already held. Returns how many were
    /// actually new.
    /// </summary>
    /// <remarks>
    /// The discarding is not a nicety. The board re-sends whenever an
    /// acknowledgement goes missing, which over a sleeping free-tier instance is
    /// routine, and the unique index on (serial, flightId, index) is what makes
    /// that free. Anything else — reading first, or trusting the board's cursor —
    /// either costs a round trip per batch or loses rows.
    /// </remarks>
    Task<int> AppendAsync(
        IReadOnlyCollection<TelemetrySample> samples, CancellationToken cancellationToken = default);

    /// <summary>The highest index held for a flight, plus one, or 0 when there is none.</summary>
    Task<int> NextIndexAsync(
        string serial, int flightId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Every row of a flight in index order, streamed. A flight is tens of
    /// thousands of rows and the export writes them straight to the response, so
    /// nothing here materialises the list.
    /// </summary>
    IAsyncEnumerable<TelemetrySample> StreamAsync(
        string serial, int flightId, CancellationToken cancellationToken = default);

    /// <summary>A window of rows for the detail page.</summary>
    Task<IReadOnlyList<TelemetrySample>> PageAsync(
        string serial, int flightId, int skip, int take,
        CancellationToken cancellationToken = default);

    Task<long> DeleteForFlightAsync(
        string serial, int flightId, CancellationToken cancellationToken = default);
}
