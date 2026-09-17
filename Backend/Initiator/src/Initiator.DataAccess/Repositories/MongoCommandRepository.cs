using Initiator.Domain.Commands;
using Initiator.Domain.States;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoCommandRepository : ICommandRepository
{
    /// <summary>A page beyond this is nobody reading, it is a browser being asked to render a database.</summary>
    private const int MaxLimit = 500;

    private readonly InitiatorDbContext _context;

    public MongoCommandRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public async Task<Command> QueueAsync(Command command, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(command);

        await _context.Commands.InsertOneAsync(command, options: null, cancellationToken);
        return command;
    }

    public async Task<Command?> ClaimNextForDeviceAsync(
        string serial, DateTime utcNow, CancellationToken cancellationToken = default)
    {
        // Anything already too old is retired before we look, so a node coming
        // back after an outage is never handed a command from before it.
        await ExpireStaleAsync(utcNow, cancellationToken);

        var filter = Builders<Command>.Filter;

        var update = Builders<Command>.Update
            .Set(command => command.Status, CommandStatus.Delivered)
            .Set(command => command.DeliveredAt, utcNow);

        return await _context.Commands.FindOneAndUpdateAsync<Command>(
            filter.And(
                filter.Eq(command => command.DeviceSerial, serial),
                filter.Eq(command => command.Status, CommandStatus.Pending)),
            update,
            new FindOneAndUpdateOptions<Command>
            {
                // Oldest first: the queue is a queue. Clicking Init then Arm and
                // having the node see them in the other order would be a
                // rejection for no reason the operator can see.
                Sort = Builders<Command>.Sort.Ascending(command => command.QueuedAt),
                ReturnDocument = ReturnDocument.After,
            },
            cancellationToken);
    }

    public async Task<Command?> GetByIdAsync(string commandId, CancellationToken cancellationToken = default)
    {
        return await _context.Commands
            .Find(command => command.Id == commandId)
            .FirstOrDefaultAsync(cancellationToken);
    }

    public async Task<Command?> ResolveAsync(
        string commandId,
        CommandStatus status,
        RejectReason reason,
        DateTime utcNow,
        CancellationToken cancellationToken = default)
    {
        var filter = Builders<Command>.Filter;

        var update = Builders<Command>.Update
            .Set(command => command.Status, status)
            .Set(command => command.Reason, reason)
            .Set(command => command.ResolvedAt, utcNow);

        // Only a command that is still in flight can be resolved. A node
        // retrying a result post it never saw acknowledged would otherwise
        // rewrite a settled record — and the retry carries the same answer, so
        // there is nothing to gain by letting it through.
        return await _context.Commands.FindOneAndUpdateAsync<Command>(
            filter.And(
                filter.Eq(command => command.Id, commandId),
                filter.In(command => command.Status, new[] { CommandStatus.Pending, CommandStatus.Delivered })),
            update,
            new FindOneAndUpdateOptions<Command> { ReturnDocument = ReturnDocument.After },
            cancellationToken);
    }

    public async Task<bool> CancelAsync(
        string commandId, DateTime utcNow, CancellationToken cancellationToken = default)
    {
        var filter = Builders<Command>.Filter;

        var update = Builders<Command>.Update
            .Set(command => command.Status, CommandStatus.Cancelled)
            .Set(command => command.ResolvedAt, utcNow);

        // Pending only. Once a node has collected a command, this service has no
        // way to recall it — saying "cancelled" then would be a comforting lie.
        var result = await _context.Commands.UpdateOneAsync(
            filter.And(
                filter.Eq(command => command.Id, commandId),
                filter.Eq(command => command.Status, CommandStatus.Pending)),
            update,
            cancellationToken: cancellationToken);

        return result.ModifiedCount > 0;
    }

    public async Task<long> ExpireStaleAsync(DateTime utcNow, CancellationToken cancellationToken = default)
    {
        var filter = Builders<Command>.Filter;
        var cutoff = utcNow - CommandPolicy.ExpiresAfter;

        var result = await _context.Commands.UpdateManyAsync(
            filter.And(
                filter.Eq(command => command.Status, CommandStatus.Pending),
                filter.Lte(command => command.QueuedAt, cutoff)),
            Builders<Command>.Update
                .Set(command => command.Status, CommandStatus.Expired)
                .Set(command => command.ResolvedAt, utcNow),
            cancellationToken: cancellationToken);

        return result.ModifiedCount;
    }

    public async Task<IReadOnlyList<Command>> QueryAsync(
        string? serial, int limit, CancellationToken cancellationToken = default)
    {
        var filter = string.IsNullOrWhiteSpace(serial)
            ? FilterDefinition<Command>.Empty
            : Builders<Command>.Filter.Eq(command => command.DeviceSerial, serial);

        return await _context.Commands
            .Find(filter)
            .SortByDescending(command => command.QueuedAt)
            .Limit(Math.Clamp(limit, 1, MaxLimit))
            .ToListAsync(cancellationToken);
    }

    public async Task<IReadOnlyList<Command>> GetOutstandingAsync(CancellationToken cancellationToken = default)
    {
        return await _context.Commands
            .Find(command => command.Status == CommandStatus.Pending)
            .SortBy(command => command.QueuedAt)
            .ToListAsync(cancellationToken);
    }

    public Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _context.Commands.DeleteManyAsync(
            command => command.DeviceSerial == serial, cancellationToken);
    }
}
