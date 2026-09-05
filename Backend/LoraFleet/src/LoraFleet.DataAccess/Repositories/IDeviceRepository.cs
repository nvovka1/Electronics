using LoraFleet.Domain.Devices;

namespace LoraFleet.DataAccess.Repositories;

public interface IDeviceRepository
{
    Task<IReadOnlyList<Device>> GetAllAsync(CancellationToken cancellationToken = default);

    Task<Device?> GetBySerialAsync(string serial, CancellationToken cancellationToken = default);

    /// <summary>
    /// Records a check-in, creating the device if this is the first time it has
    /// been heard from. Enrolment is implicit on purpose: a node that reaches
    /// the backend has already proved it exists, and making an operator
    /// pre-register forty serial numbers by hand invites typos in the one field
    /// that has to match the hardware exactly.
    /// </summary>
    Task<Device> UpsertCheckInAsync(Device device, CancellationToken cancellationToken = default);

    Task SetTargetFirmwareAsync(string serial, string? targetVersion, CancellationToken cancellationToken = default);

    Task SetNotesAsync(string serial, string? notes, CancellationToken cancellationToken = default);

    Task SetDecommissionedAsync(string serial, bool isDecommissioned, CancellationToken cancellationToken = default);

    Task DeleteAsync(string serial, CancellationToken cancellationToken = default);
}
