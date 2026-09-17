using Initiator.Domain.Devices;
using Initiator.Domain.States;
using MongoDB.Driver;

namespace Initiator.DataAccess.Repositories;

public sealed class MongoDeviceRepository : IDeviceRepository
{
    private readonly InitiatorDbContext _context;

    public MongoDeviceRepository(InitiatorDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public async Task<IReadOnlyList<Device>> GetAllAsync(CancellationToken cancellationToken = default)
    {
        // Newest check-in first: whoever just came back, or just fell over, is
        // what an operator opening the page is looking for.
        return await _context.Devices
            .Find(FilterDefinition<Device>.Empty)
            .SortByDescending(device => device.LastSeenAt)
            .ToListAsync(cancellationToken);
    }

    public async Task<Device?> GetBySerialAsync(string serial, CancellationToken cancellationToken = default)
    {
        return await _context.Devices
            .Find(device => device.Serial == serial)
            .FirstOrDefaultAsync(cancellationToken);
    }

    public async Task<Device> UpsertCheckInAsync(Device device, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(device);

        // Only the facts a node reports about itself are written here. Notes and
        // IsDecommissioned belong to the operator, and a check-in must never
        // overwrite them. The state fields are absent on purpose too: they go
        // through ApplyReportedStateAsync, which knows how to ignore a stale
        // report.
        var update = Builders<Device>.Update
            .Set(existing => existing.NodeId, device.NodeId)
            .Set(existing => existing.HardwareId, device.HardwareId)
            .Set(existing => existing.FirmwareVersion, device.FirmwareVersion)
            .Set(existing => existing.FirmwareHash, device.FirmwareHash)
            .Set(existing => existing.FirmwareIsDirty, device.FirmwareIsDirty)
            .Set(existing => existing.BuildType, device.BuildType)
            .Set(existing => existing.ProtocolVersion, device.ProtocolVersion)
            .Set(existing => existing.AutoArmTimeoutSeconds, device.AutoArmTimeoutSeconds)
            .Set(existing => existing.LastSeenAt, device.LastSeenAt)
            .Set(existing => existing.LastHealth, device.LastHealth)
            .SetOnInsert(existing => existing.EnrolledAt, device.LastSeenAt)

            // A node that has never reported a state is in SAFE, because that is
            // what it booted into. Set only on insert: on every later check-in
            // the stored state is more informed than this default.
            .SetOnInsert(existing => existing.CurrentState, NodeStateMachine.BootState)
            .SetOnInsert(existing => existing.StateChangedAt, device.LastSeenAt)
            .SetOnInsert(existing => existing.StateSource, CommandSource.Timer)
            .SetOnInsert(existing => existing.StateBootCount, 0)
            .SetOnInsert(existing => existing.StateTimestampMs, 0L);

        return await _context.Devices.FindOneAndUpdateAsync<Device>(
            existing => existing.Serial == device.Serial,
            update,
            new FindOneAndUpdateOptions<Device>
            {
                IsUpsert = true,
                ReturnDocument = ReturnDocument.After,
            },
            cancellationToken);
    }

    public async Task<Device?> ApplyReportedStateAsync(
        string serial,
        NodeState state,
        CommandSource source,
        int bootCount,
        long timestampMs,
        DateTime observedAt,
        CancellationToken cancellationToken = default)
    {
        var filter = Builders<Device>.Filter;

        // The guard lives in the query rather than in a read-then-write, so two
        // reports arriving at once cannot both decide they are the newest. A
        // higher boot count always wins: the node's clock restarts at zero on
        // every boot, so comparing milliseconds alone would treat a node that
        // has just rebooted as ancient history and ignore it forever.
        var isNewer = filter.Or(
            filter.Lt(device => device.StateBootCount, bootCount),
            filter.And(
                filter.Eq(device => device.StateBootCount, bootCount),
                filter.Lte(device => device.StateTimestampMs, timestampMs)));

        var update = Builders<Device>.Update
            .Set(device => device.CurrentState, state)
            .Set(device => device.StateSource, source)
            .Set(device => device.StateChangedAt, observedAt)
            .Set(device => device.StateBootCount, bootCount)
            .Set(device => device.StateTimestampMs, timestampMs);

        var updated = await _context.Devices.FindOneAndUpdateAsync<Device>(
            filter.And(filter.Eq(device => device.Serial, serial), isNewer),
            update,
            new FindOneAndUpdateOptions<Device> { ReturnDocument = ReturnDocument.After },
            cancellationToken);

        // No match means either the device is unknown or the report was stale.
        // Those need different answers, and only a second read can tell them
        // apart — it happens on the uncommon path, not on every report.
        return updated ?? await GetBySerialAsync(serial, cancellationToken);
    }

    public Task SetNotesAsync(string serial, string? notes, CancellationToken cancellationToken = default)
    {
        return _context.Devices.UpdateOneAsync(
            device => device.Serial == serial,
            Builders<Device>.Update.Set(device => device.Notes, notes),
            cancellationToken: cancellationToken);
    }

    public Task SetDecommissionedAsync(
        string serial, bool isDecommissioned, CancellationToken cancellationToken = default)
    {
        return _context.Devices.UpdateOneAsync(
            device => device.Serial == serial,
            Builders<Device>.Update.Set(device => device.IsDecommissioned, isDecommissioned),
            cancellationToken: cancellationToken);
    }

    public Task DeleteAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _context.Devices.DeleteOneAsync(device => device.Serial == serial, cancellationToken);
    }
}
