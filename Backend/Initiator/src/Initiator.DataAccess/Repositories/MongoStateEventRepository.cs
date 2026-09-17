using Initiator.Domain.States;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoStateEventRepository : IStateEventRepository
{
    private const int MaxLimit = 1000;

    private readonly InitiatorDbContext _context;

    public MongoStateEventRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public Task AppendAsync(
        IReadOnlyCollection<StateEvent> events, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(events);
        if (events.Count == 0) return Task.CompletedTask;

        // Unordered, so one malformed event in a batch does not stop the rest of
        // a node's history from being stored. A node flushing a backlog after an
        // outage sends these in bulk, and losing the whole flush would lose the
        // record of exactly the period nobody was watching.
        return _context.StateEvents.InsertManyAsync(
            events,
            new InsertManyOptions { IsOrdered = false },
            cancellationToken);
    }

    public async Task<IReadOnlyList<StateEvent>> QueryAsync(
        string? serial, int limit, CancellationToken cancellationToken = default)
    {
        var filter = string.IsNullOrWhiteSpace(serial)
            ? FilterDefinition<StateEvent>.Empty
            : Builders<StateEvent>.Filter.Eq(stateEvent => stateEvent.DeviceSerial, serial);

        return await _context.StateEvents
            .Find(filter)
            .SortByDescending(stateEvent => stateEvent.ReceivedAt)
            .ThenByDescending(stateEvent => stateEvent.TimestampMs)
            .Limit(Math.Clamp(limit, 1, MaxLimit))
            .ToListAsync(cancellationToken);
    }

    public Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _context.StateEvents.DeleteManyAsync(
            stateEvent => stateEvent.DeviceSerial == serial, cancellationToken);
    }
}
