using Initiator.Web.Contracts;

namespace Initiator.Web.Services;

/// <param name="Stored">How many events were understood and kept.</param>
/// <param name="Discarded">How many carried values this build does not recognise.</param>
public readonly record struct IngestOutcome(int Stored, int Discarded);

public interface IStateEventIngestService
{
    /// <summary>
    /// Stores a batch of transitions, moves the device to the newest state in
    /// it, and closes any queued commands the batch resolves.
    /// </summary>
    Task<IngestOutcome> RecordAsync(
        string serial, StateEventBatchRequest request, CancellationToken cancellationToken = default);
}
