using Initiator.DataAccess.Repositories;
using Initiator.Domain.Commands;
using Initiator.Domain.States;
using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;

namespace Initiator.Web.Services;

public sealed class StateEventIngestService : IStateEventIngestService
{
    private readonly IStateEventRepository _events;
    private readonly IDeviceRepository _devices;
    private readonly ICommandRepository _commands;
    private readonly TimeProvider _clock;
    private readonly ILogger<StateEventIngestService> _logger;

    public StateEventIngestService(
        IStateEventRepository events,
        IDeviceRepository devices,
        ICommandRepository commands,
        TimeProvider clock,
        ILogger<StateEventIngestService> logger)
    {
        _events = events ?? throw new ArgumentNullException(nameof(events));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _commands = commands ?? throw new ArgumentNullException(nameof(commands));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task<IngestOutcome> RecordAsync(
        string serial, StateEventBatchRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(serial);
        ArgumentNullException.ThrowIfNull(request);

        var now = _clock.GetUtcNow().UtcDateTime;

        var stored = new List<StateEvent>(request.Events.Count);
        var discarded = 0;

        foreach (var incoming in request.Events)
        {
            var mapped = Map(serial, incoming, now);

            if (mapped is null)
            {
                discarded++;
                continue;
            }

            stored.Add(mapped);
        }

        if (discarded > 0)
        {
            _logger.LogWarning(
                "Discarded {Count} state event(s) from {Serial} carrying values this build " +
                "does not recognise; firmware and backend may disagree about the protocol",
                discarded, serial);
        }

        if (stored.Count == 0) return new IngestOutcome(0, discarded);

        await _events.AppendAsync(stored, cancellationToken);

        await ApplyNewestStateAsync(serial, stored, now, cancellationToken);
        await ResolveReferencedCommandsAsync(stored, now, cancellationToken);

        return new IngestOutcome(stored.Count, discarded);
    }

    /// <summary>
    /// Returns null when the event carries a state, command or source this build
    /// does not know — better to drop one record and say so than to store a
    /// value that renders as "state200" on the dashboard.
    /// </summary>
    private StateEvent? Map(string serial, StateEventRequest incoming, DateTime now)
    {
        if (!WireValues.TryState(incoming.FromState, out var fromState)) return null;
        if (!WireValues.TryState(incoming.ToState, out var toState)) return null;
        if (!WireValues.TrySource(incoming.Source, out var source)) return null;

        CommandType? command = null;

        if (incoming.Command.HasValue)
        {
            if (!WireValues.TryCommand(incoming.Command.Value, out var parsed)) return null;
            command = parsed;
        }

        return new StateEvent
        {
            DeviceSerial = serial,
            TimestampMs = incoming.TimestampMs,
            BootCount = incoming.BootCount,
            ReceivedAt = now,
            FromState = fromState,
            ToState = toState,
            Command = command,
            Source = source,
            Accepted = incoming.Accepted,
            Reason = WireValues.Reason(incoming.Reason),
            CommandId = string.IsNullOrWhiteSpace(incoming.CommandId) ? null : incoming.CommandId,
        };
    }

    /// <summary>
    /// Moves the device to the state reported by the newest event in the batch.
    /// </summary>
    /// <remarks>
    /// Every event carries the state the node was in afterwards, including the
    /// rejections — a refused command leaves the node where it was, and saying
    /// so is still a fact about the node's state. So the newest event in the
    /// batch is the best answer regardless of whether it was accepted, and the
    /// repository's own guard still rejects it if an even newer report has
    /// already landed by another route.
    /// </remarks>
    private async Task ApplyNewestStateAsync(
        string serial, List<StateEvent> events, DateTime now, CancellationToken cancellationToken)
    {
        // A node flushing a backlog sends them oldest-first, but nothing
        // guarantees that, so the newest is found rather than assumed.
        var newest = events
            .OrderBy(stateEvent => stateEvent.BootCount)
            .ThenBy(stateEvent => stateEvent.TimestampMs)
            .Last();

        await _devices.ApplyReportedStateAsync(
            serial,
            newest.ToState,
            newest.Source,
            newest.BootCount,
            newest.TimestampMs,
            now,
            cancellationToken);
    }

    private async Task ResolveReferencedCommandsAsync(
        List<StateEvent> events, DateTime now, CancellationToken cancellationToken)
    {
        foreach (var stateEvent in events.Where(candidate => candidate.CommandId is not null))
        {
            await _commands.ResolveAsync(
                stateEvent.CommandId!,
                stateEvent.Accepted ? CommandStatus.Applied : CommandStatus.Rejected,
                stateEvent.Reason,
                now,
                cancellationToken);
        }
    }
}
