using Initiator.DataAccess.Repositories;
using Initiator.Domain.Commands;
using Initiator.Domain.States;
using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;

namespace Initiator.Web.Services;

public sealed class CommandDispatchService : ICommandDispatchService
{
    private readonly ICommandRepository _commands;
    private readonly IDeviceRepository _devices;
    private readonly TimeProvider _clock;
    private readonly ILogger<CommandDispatchService> _logger;

    public CommandDispatchService(
        ICommandRepository commands,
        IDeviceRepository devices,
        TimeProvider clock,
        ILogger<CommandDispatchService> logger)
    {
        _commands = commands ?? throw new ArgumentNullException(nameof(commands));
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task<QueueOutcome> QueueAsync(
        string serial, CommandType type, string? note, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(serial);

        var device = await _devices.GetBySerialAsync(serial, cancellationToken);

        if (device is null)
            return QueueOutcome.Refused($"No device is enrolled with serial {serial}.");

        if (device.IsDecommissioned)
            return QueueOutcome.Refused($"{serial} is decommissioned.");

        // Refusing here is a courtesy, not an authority: it tells the operator
        // now instead of one poll interval later. The node judges the command
        // again against its own state, which may have moved on since this
        // service last heard from it.
        if (!CommandPolicy.CanQueue(device.CurrentState, type))
        {
            return QueueOutcome.Refused(
                $"{serial} is in {device.CurrentState.ToString().ToUpperInvariant()}, " +
                $"which does not accept {type.ToString().ToUpperInvariant()}.");
        }

        var command = new Command
        {
            DeviceSerial = serial,
            Type = type,
            Status = CommandStatus.Pending,
            QueuedAt = _clock.GetUtcNow().UtcDateTime,
            QueuedAgainstState = device.CurrentState,
            Note = string.IsNullOrWhiteSpace(note) ? null : note.Trim(),
        };

        await _commands.QueueAsync(command, cancellationToken);

        _logger.LogInformation(
            "Queued {Command} for {Serial} against state {State}",
            type, serial, device.CurrentState);

        return QueueOutcome.Accepted(command);
    }

    public Task<Command?> ClaimNextAsync(string serial, CancellationToken cancellationToken = default)
    {
        return _commands.ClaimNextForDeviceAsync(
            serial, _clock.GetUtcNow().UtcDateTime, cancellationToken);
    }

    public async Task<Command?> ResolveAsync(
        string serial,
        string commandId,
        CommandResultRequest result,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(result);

        var now = _clock.GetUtcNow().UtcDateTime;

        var resolved = await _commands.ResolveAsync(
            commandId,
            result.Accepted ? CommandStatus.Applied : CommandStatus.Rejected,
            WireValues.Reason(result.Reason),
            now,
            cancellationToken);

        // The node tells us the state it ended in, whether or not it obeyed. A
        // rejection is the more valuable of the two: it means this service's
        // view was stale, and the state it sends back is the correction.
        if (WireValues.TryState(result.State, out var state))
        {
            await _devices.ApplyReportedStateAsync(
                serial,
                state,
                CommandSource.Api,
                result.BootCount,
                result.TimestampMs,
                now,
                cancellationToken);
        }

        if (resolved is null)
        {
            // Either an id we never issued, or one already settled. Both are
            // benign — a node retrying a post it never saw acknowledged is the
            // common cause — but worth a line when chasing a mismatch.
            _logger.LogInformation(
                "Result for command {CommandId} from {Serial} changed nothing; " +
                "it is unknown or already resolved", commandId, serial);
        }

        return resolved;
    }

    public Task<bool> CancelAsync(string commandId, CancellationToken cancellationToken = default)
    {
        return _commands.CancelAsync(commandId, _clock.GetUtcNow().UtcDateTime, cancellationToken);
    }
}
