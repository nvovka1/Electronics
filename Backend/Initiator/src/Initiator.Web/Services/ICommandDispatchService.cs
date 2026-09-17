using Initiator.Domain.Commands;
using Initiator.Domain.States;
using Initiator.Web.Contracts;

namespace Initiator.Web.Services;

/// <summary>Why a command could not be queued, or null when it was.</summary>
/// <param name="Command">The queued command, when it was accepted.</param>
/// <param name="Problem">A sentence for the operator, when it was not.</param>
public readonly record struct QueueOutcome(Command? Command, string? Problem)
{
    public bool Queued => Command is not null;

    public static QueueOutcome Accepted(Command command) => new(command, null);

    public static QueueOutcome Refused(string problem) => new(null, problem);
}

public interface ICommandDispatchService
{
    /// <summary>
    /// Queues a command for a node to collect on its next poll, refusing one
    /// that the node's last known state would not accept.
    /// </summary>
    Task<QueueOutcome> QueueAsync(
        string serial, CommandType type, string? note, CancellationToken cancellationToken = default);

    /// <summary>Hands a node its next command, or null when the queue is empty.</summary>
    Task<Command?> ClaimNextAsync(string serial, CancellationToken cancellationToken = default);

    /// <summary>Records what a node did with a command, and what state it left it in.</summary>
    Task<Command?> ResolveAsync(
        string serial,
        string commandId,
        CommandResultRequest result,
        CancellationToken cancellationToken = default);

    Task<bool> CancelAsync(string commandId, CancellationToken cancellationToken = default);
}
