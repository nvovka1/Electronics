using LoraFleet.Domain.Logs;
using MongoDB.Driver;

namespace LoraFleet.DataAccess.Repositories;

public sealed class MongoLogRepository : ILogRepository
{
    /// <summary>A page beyond this is nobody reading, it is a browser being asked to render a database.</summary>
    private const int MaxLimit = 2000;

    private readonly FleetDbContext _context;

    public MongoLogRepository(FleetDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public Task AppendAsync(IReadOnlyCollection<LogRecord> records, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(records);
        if (records.Count == 0) return Task.CompletedTask;

        // Unordered, because one malformed record in a batch must not stop the
        // rest of a node's log from being stored.
        return _context.Logs.InsertManyAsync(
            records,
            new InsertManyOptions { IsOrdered = false },
            cancellationToken);
    }

    public async Task<IReadOnlyList<LogRecord>> QueryAsync(LogQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);

        var limit = Math.Clamp(query.Limit, 1, MaxLimit);

        return await _context.Logs
            .Find(BuildFilter(query))
            .SortByDescending(record => record.ReceivedAt)
            .ThenByDescending(record => record.TimestampMs)
            .Limit(limit)
            .ToListAsync(cancellationToken);
    }

    public Task<long> CountAsync(LogQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        return _context.Logs.CountDocumentsAsync(BuildFilter(query), cancellationToken: cancellationToken);
    }

    public Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _context.Logs.DeleteManyAsync(record => record.DeviceSerial == serial, cancellationToken);
    }

    private static FilterDefinition<LogRecord> BuildFilter(LogQuery query)
    {
        var builder = Builders<LogRecord>.Filter;
        var filters = new List<FilterDefinition<LogRecord>>();

        if (!string.IsNullOrWhiteSpace(query.DeviceSerial))
            filters.Add(builder.Eq(record => record.DeviceSerial, query.DeviceSerial));

        // Lower numbers are more severe, so "at most this level" is the natural
        // way to ask for "this severity and worse".
        if (query.MaxLevel.HasValue)
            filters.Add(builder.Lte(record => record.Level, query.MaxLevel.Value));

        if (query.Tag.HasValue)
            filters.Add(builder.Eq(record => record.Tag, query.Tag.Value));

        if (query.Since.HasValue)
            filters.Add(builder.Gte(record => record.ReceivedAt, query.Since.Value));

        return filters.Count == 0 ? builder.Empty : builder.And(filters);
    }
}
