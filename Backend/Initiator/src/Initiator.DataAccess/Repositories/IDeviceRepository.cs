using Initiator.Domain.Devices;
using Initiator.Domain.States;

namespace Initiator.DataAccess.Repositories;

public interface IDeviceRepository
{
    Task<IReadOnlyList<Device>> GetAllAsync(CancellationToken cancellationToken = default);

    Task<Device?> GetBySerialAsync(string serial, CancellationToken cancellationToken = default);

    /// <summary>
    /// Records a check-in, creating the device if this is the first time it has
    /// been heard from.
    /// </summary>
    /// <remarks>
    /// Enrolment is implicit on purpose: a node that reaches this service has
    /// already proved it exists, and making an operator pre-register serial
    /// numbers by hand invites a typo in the one field that has to match the
    /// hardware exactly.
    /// </remarks>
    Task<Device> UpsertCheckInAsync(Device device, CancellationToken cancellationToken = default);

    /// <summary>
    /// Records what state a node says it is in, but only when the report is
    /// newer than what is already stored.
    /// </summary>
    /// <param name="bootCount">The node's boot count when it observed this state.</param>
    /// <param name="timestampMs">The node's monotonic clock at that moment.</param>
    /// <returns>
    /// The device as it now stands, or null if it is not enrolled. Whether the
    /// state was actually taken is visible in the returned device.
    /// </returns>
    Task<Device?> ApplyReportedStateAsync(
        string serial,
        NodeState state,
        CommandSource source,
        int bootCount,
        long timestampMs,
        DateTime observedAt,
        CancellationToken cancellationToken = default);

    Task SetNotesAsync(string serial, string? notes, CancellationToken cancellationToken = default);

    Task SetDecommissionedAsync(string serial, bool isDecommissioned, CancellationToken cancellationToken = default);

    Task DeleteAsync(string serial, CancellationToken cancellationToken = default);
}
