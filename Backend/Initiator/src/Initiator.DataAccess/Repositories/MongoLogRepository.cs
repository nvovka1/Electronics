using Initiator.Domain.Logs;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoLogRepository : ILogRepository
{
    private const int MaxLimit = 2000;

    private readonly InitiatorDbContext _context;

    public MongoLogRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public Task AppendAsync(
        IReadOnlyCollection<LogRecord> records, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(records);
        if (records.Count == 0) return Task.CompletedTask;

        return _context.Logs.InsertManyAsync(
            records,
            new InsertManyOptions { IsOrdered = false },
            cancellationToken);
    }

    public async Task<IReadOnlyList<LogRecord>> QueryAsync(
        LogQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);

        return await _context.Logs
            .Find(BuildFilter(query))
            .SortByDescending(record => record.ReceivedAt)
            .ThenByDescending(record => record.TimestampMs)
            .Limit(Math.Clamp(query.Limit, 1, MaxLimit))
            .ToListAsync(cancellationToken);
    }

    public Task<long> CountAsync(LogQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        return _context.Logs.CountDocumentsAsync(
            BuildFilter(query), cancellationToken: cancellationToken);
    }

    public Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _context.Logs.DeleteManyAsync(
            record => record.DeviceSerial == serial, cancellationToken);
    }

    private static FilterDefinition<LogRecord> BuildFilter(LogQuery query)
    {
        var builder = Builders<LogRecord>.Filter;
        var filters = new List<FilterDefinition<LogRecord>>();

        if (!string.IsNullOrWhiteSpace(query.DeviceSerial))
            filters.Add(builder.Eq(record => record.DeviceSerial, query.DeviceSerial));

        if (query.MaxLevel.HasValue)
            filters.Add(builder.Lte(record => record.Level, query.MaxLevel.Value));

        if (query.Tag.HasValue)
            filters.Add(builder.Eq(record => record.Tag, query.Tag.Value));

        if (query.Since.HasValue)
            filters.Add(builder.Gte(record => record.ReceivedAt, query.Since.Value));

        return filters.Count == 0 ? builder.Empty : builder.And(filters);
    }
}
