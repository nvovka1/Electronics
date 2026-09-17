using Initiator.Domain.States;

namespace Initiator.DataAccess.Repositories;

public interface IStateEventRepository
{
    Task AppendAsync(
        IReadOnlyCollection<StateEvent> events, CancellationToken cancellationToken = default);

    /// <summary>Newest first. The device page shows the last handful; the history page shows more.</summary>
    Task<IReadOnlyList<StateEvent>> QueryAsync(
        string? serial, int limit, CancellationToken cancellationToken = default);

    Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default);
}
