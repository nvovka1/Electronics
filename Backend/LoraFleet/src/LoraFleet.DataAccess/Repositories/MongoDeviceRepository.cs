using LoraFleet.Domain.Devices;
using MongoDB.Driver;

namespace LoraFleet.DataAccess.Repositories;

public sealed class MongoDeviceRepository : IDeviceRepository
{
    private readonly FleetDbContext _context;

    public MongoDeviceRepository(FleetDbContext context)
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

        // Only the fields a device reports about itself are written here.
        // TargetFirmwareVersion, Notes and IsDecommissioned belong to the
        // operator, and a check-in must never overwrite them.
        var update = Builders<Device>.Update
            .Set(existing => existing.NodeId, device.NodeId)
            .Set(existing => existing.HardwareId, device.HardwareId)
            .Set(existing => existing.FirmwareVersion, device.FirmwareVersion)
            .Set(existing => existing.FirmwareHash, device.FirmwareHash)
            .Set(existing => existing.FirmwareIsDirty, device.FirmwareIsDirty)
            .Set(existing => existing.BuildType, device.BuildType)
            .Set(existing => existing.ProtocolVersion, device.ProtocolVersion)
            .Set(existing => existing.ConfigVersion, device.ConfigVersion)
            .Set(existing => existing.LastSeenAt, device.LastSeenAt)
            .Set(existing => existing.LastHealth, device.LastHealth)
            .SetOnInsert(existing => existing.EnrolledAt, device.LastSeenAt);

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

    public Task SetTargetFirmwareAsync(string serial, string? targetVersion, CancellationToken cancellationToken = default)
    {
        return _context.Devices.UpdateOneAsync(
            device => device.Serial == serial,
            Builders<Device>.Update.Set(device => device.TargetFirmwareVersion, targetVersion),
            cancellationToken: cancellationToken);
    }

    public Task SetNotesAsync(string serial, string? notes, CancellationToken cancellationToken = default)
    {
        return _context.Devices.UpdateOneAsync(
            device => device.Serial == serial,
            Builders<Device>.Update.Set(device => device.Notes, notes),
            cancellationToken: cancellationToken);
    }

    public Task SetDecommissionedAsync(string serial, bool isDecommissioned, CancellationToken cancellationToken = default)
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
