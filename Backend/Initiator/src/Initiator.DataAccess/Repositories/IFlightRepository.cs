using Initiator.Domain.Telemetry;

namespace Initiator.DataAccess.Repositories;

public interface IFlightRepository
{
    /// <summary>
    /// Creates or updates the flight's summary from a batch that has just been
    /// stored, and returns it. The aircraft enrols itself here: the first batch
    /// carrying a serial creates its first flight, and there is no list of
    /// aircraft to maintain by hand.
    /// </summary>
    /// <remarks>
    /// <paramref name="newRowCount"/> is how many rows were actually new, which
    /// is not the same as <paramref name="samples"/>.Count. The board re-sends
    /// whole batches whenever an acknowledgement goes missing, and counting the
    /// request rather than what it added would inflate the row count of every
    /// flight that ever met a slow response.
    /// </remarks>
    Task<Flight> RecordBatchAsync(
        string serial,
        int flightId,
        IReadOnlyCollection<TelemetrySample> samples,
        int newRowCount,
        int nextIndex,
        DateTime utcNow,
        CancellationToken cancellationToken = default);

    Task<Flight?> GetAsync(string serial, int flightId, CancellationToken cancellationToken = default);

    /// <summary>Newest first. A null serial means every aircraft's.</summary>
    Task<IReadOnlyList<Flight>> ListAsync(
        string? serial, int limit, CancellationToken cancellationToken = default);

    /// <summary>One row per aircraft, aggregated over its flights.</summary>
    Task<IReadOnlyList<AircraftSummary>> ListAircraftAsync(
        CancellationToken cancellationToken = default);

    /// <summary>
    /// The flights beyond the newest <paramref name="keep"/> for an aircraft,
    /// oldest first. Used by ingest to hold the collection to a size the free
    /// tier can carry.
    /// </summary>
    Task<IReadOnlyList<Flight>> ListBeyondAsync(
        string serial, int keep, CancellationToken cancellationToken = default);

    Task<bool> DeleteAsync(string serial, int flightId, CancellationToken cancellationToken = default);
}
