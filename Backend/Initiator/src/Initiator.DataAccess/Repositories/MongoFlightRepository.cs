using Initiator.Domain.Telemetry;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoFlightRepository : IFlightRepository
{
    private const int MaxLimit = 500;

    private readonly InitiatorDbContext _context;

    public MongoFlightRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public static string BuildId(string serial, int flightId) => $"{serial}:{flightId}";

    public async Task<Flight> RecordBatchAsync(
        string serial,
        int flightId,
        IReadOnlyCollection<TelemetrySample> samples,
        int newRowCount,
        int nextIndex,
        DateTime utcNow,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(samples);

        var id = BuildId(serial, flightId);
        var update = Builders<Flight>.Update
            .SetOnInsert(flight => flight.Serial, serial)
            .SetOnInsert(flight => flight.FlightId, flightId)
            .SetOnInsert(flight => flight.FirstSeenAt, utcNow)
            .Set(flight => flight.LastSampleAt, utcNow)
            .Set(flight => flight.NextIndex, nextIndex);

        // What was actually added, not what was sent. A re-sent batch adds
        // nothing, and counting the request instead would inflate the row count
        // of every flight that ever met a slow response.
        if (newRowCount > 0)
        {
            update = update.Inc(flight => flight.SampleCount, newRowCount);
        }

        var stamps = samples.Where(sample => sample.Utc.HasValue)
                            .Select(sample => sample.Utc!.Value)
                            .ToList();

        if (stamps.Count > 0)
        {
            // Min, not "the first batch's first row": batches can arrive out of
            // order after a rewind, and a start time that moves later every time
            // the board resends would be worse than none.
            update = update.Min(flight => flight.StartedAt, stamps.Min())
                           .Max(flight => flight.EndedAt, stamps.Max());
        }

        var elapsed = samples.Count > 0 ? samples.Max(sample => sample.TMs) : 0;
        if (elapsed > 0) update = update.Max(flight => flight.DurationMs, elapsed);

        update = ApplyExtreme(update, samples, sample => sample.AltRel, flight => flight.MaxAltRel, true);
        update = ApplyExtreme(update, samples, sample => sample.GroundSpeed, flight => flight.MaxGroundSpeed, true);
        update = ApplyExtreme(update, samples, sample => sample.BatteryVoltage, flight => flight.MinBatteryVoltage, false);

        return await _context.Flights.FindOneAndUpdateAsync<Flight>(
            flight => flight.Id == id,
            update,
            new FindOneAndUpdateOptions<Flight>
            {
                IsUpsert = true,
                ReturnDocument = ReturnDocument.After,
            },
            cancellationToken);
    }

    public Task<Flight?> GetAsync(
        string serial, int flightId, CancellationToken cancellationToken = default)
    {
        var id = BuildId(serial, flightId);
        return _context.Flights.Find(flight => flight.Id == id).FirstOrDefaultAsync(cancellationToken)!;
    }

    public async Task<IReadOnlyList<Flight>> ListAsync(
        string? serial, int limit, CancellationToken cancellationToken = default)
    {
        var filter = string.IsNullOrWhiteSpace(serial)
            ? Builders<Flight>.Filter.Empty
            : Builders<Flight>.Filter.Eq(flight => flight.Serial, serial);

        return await _context.Flights
            .Find(filter)
            .SortByDescending(flight => flight.LastSampleAt)
            .Limit(Math.Clamp(limit, 1, MaxLimit))
            .ToListAsync(cancellationToken);
    }

    public async Task<IReadOnlyList<AircraftSummary>> ListAircraftAsync(
        CancellationToken cancellationToken = default)
    {
        // An aggregate rather than a collection: see Flight for why there is no
        // aircraft document to keep in step with this.
        var results = await _context.Flights.Aggregate()
            .Group(flight => flight.Serial,
                   group => new
                   {
                       Serial = group.Key,
                       FlightCount = group.Count(),
                       SampleCount = group.Sum(flight => (long)flight.SampleCount),
                       LastSampleAt = group.Max(flight => flight.LastSampleAt),
                       LastFlightId = group.Max(flight => flight.FlightId),
                   })
            .ToListAsync(cancellationToken);

        return results
            .Select(result => new AircraftSummary
            {
                Serial = result.Serial,
                FlightCount = result.FlightCount,
                SampleCount = result.SampleCount,
                LastSampleAt = result.LastSampleAt,
                LastFlightId = result.LastFlightId,
            })
            .OrderByDescending(summary => summary.LastSampleAt)
            .ToList();
    }

    public async Task<IReadOnlyList<Flight>> ListBeyondAsync(
        string serial, int keep, CancellationToken cancellationToken = default)
    {
        if (keep < 1) keep = 1;

        return await _context.Flights
            .Find(flight => flight.Serial == serial)
            .SortByDescending(flight => flight.FlightId)
            .Skip(keep)
            .ToListAsync(cancellationToken);
    }

    public async Task<bool> DeleteAsync(
        string serial, int flightId, CancellationToken cancellationToken = default)
    {
        var id = BuildId(serial, flightId);
        var result = await _context.Flights.DeleteOneAsync(flight => flight.Id == id, cancellationToken);
        return result.DeletedCount > 0;
    }

    /// <summary>
    /// Folds one column's extreme into the update, skipping the nulls. A null is
    /// "nobody reported it", and Mongo's $min would happily take it as the
    /// smallest value there is.
    /// </summary>
    private static UpdateDefinition<Flight> ApplyExtreme(
        UpdateDefinition<Flight> update,
        IReadOnlyCollection<TelemetrySample> samples,
        Func<TelemetrySample, double?> select,
        System.Linq.Expressions.Expression<Func<Flight, double?>> field,
        bool takeMaximum)
    {
        var values = samples.Select(select).Where(value => value.HasValue).Select(value => value!.Value).ToList();
        if (values.Count == 0) return update;

        return takeMaximum
            ? update.Max(field, values.Max())
            : update.Min(field, values.Min());
    }
}
