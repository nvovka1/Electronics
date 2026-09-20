using System.Runtime.CompilerServices;
using Initiator.Domain.Telemetry;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoTelemetrySampleRepository : ITelemetrySampleRepository
{
    /// <summary>Mongo's duplicate-key error. The only bulk-write failure this repository treats as success.</summary>
    private const int DuplicateKeyCode = 11000;

    private readonly InitiatorDbContext _context;

    public MongoTelemetrySampleRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public async Task<int> AppendAsync(
        IReadOnlyCollection<TelemetrySample> samples, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(samples);
        if (samples.Count == 0) return 0;

        try
        {
            // Unordered, so one duplicate does not stop the rows behind it. An
            // ordered insert would abandon the batch at the first row the
            // service already had, which is the first row of every retry.
            await _context.TelemetrySamples.InsertManyAsync(
                samples,
                new InsertManyOptions { IsOrdered = false },
                cancellationToken);

            return samples.Count;
        }
        catch (MongoBulkWriteException<TelemetrySample> exception)
        {
            var duplicates = exception.WriteErrors.Count(error => error.Code == DuplicateKeyCode);

            // Anything that is not a duplicate is a real failure and the caller
            // needs to know, so the board retries rather than moving its cursor
            // past rows that never landed.
            if (duplicates != exception.WriteErrors.Count) throw;

            return samples.Count - duplicates;
        }
    }

    public async Task<int> NextIndexAsync(
        string serial, int flightId, CancellationToken cancellationToken = default)
    {
        var highest = await _context.TelemetrySamples
            .Find(sample => sample.Serial == serial && sample.FlightId == flightId)
            .SortByDescending(sample => sample.Index)
            .Limit(1)
            .FirstOrDefaultAsync(cancellationToken);

        return highest is null ? 0 : highest.Index + 1;
    }

    public async IAsyncEnumerable<TelemetrySample> StreamAsync(
        string serial, int flightId,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        using var cursor = await _context.TelemetrySamples
            .Find(sample => sample.Serial == serial && sample.FlightId == flightId)
            .SortBy(sample => sample.Index)
            .ToCursorAsync(cancellationToken);

        while (await cursor.MoveNextAsync(cancellationToken))
        {
            foreach (var sample in cursor.Current)
            {
                yield return sample;
            }
        }
    }

    public async Task<IReadOnlyList<TelemetrySample>> PageAsync(
        string serial, int flightId, int skip, int take,
        CancellationToken cancellationToken = default)
    {
        return await _context.TelemetrySamples
            .Find(sample => sample.Serial == serial && sample.FlightId == flightId)
            .SortBy(sample => sample.Index)
            .Skip(Math.Max(0, skip))
            .Limit(Math.Clamp(take, 1, 2000))
            .ToListAsync(cancellationToken);
    }

    public async Task<long> DeleteForFlightAsync(
        string serial, int flightId, CancellationToken cancellationToken = default)
    {
        var result = await _context.TelemetrySamples.DeleteManyAsync(
            sample => sample.Serial == serial && sample.FlightId == flightId, cancellationToken);

        return result.DeletedCount;
    }
}
