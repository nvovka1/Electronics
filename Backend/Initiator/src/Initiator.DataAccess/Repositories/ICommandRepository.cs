using Initiator.Domain.Commands;
using Initiator.Domain.States;

namespace Initiator.DataAccess.Repositories;

public interface ICommandRepository
{
    Task<Command> QueueAsync(Command command, CancellationToken cancellationToken = default);

    /// <summary>
    /// Hands a node the oldest command waiting for it and marks it delivered, in
    /// one atomic step, or null when there is nothing to do.
    /// </summary>
    /// <remarks>
    /// Atomic because it has to be: a node that retries a poll it never saw the
    /// answer to, or two instances of this service behind a load balancer, must
    /// not both be handed the same command. Anything that is already too old to
    /// deliver is expired first, in the same call.
    /// </remarks>
    Task<Command?> ClaimNextForDeviceAsync(
        string serial, DateTime utcNow, CancellationToken cancellationToken = default);

    Task<Command?> GetByIdAsync(string commandId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Records what the node did with a command it collected. Ignores a command
    /// that has already reached a terminal status, so a retried result post
    /// cannot rewrite history.
    /// </summary>
    Task<Command?> ResolveAsync(
        string commandId,
        CommandStatus status,
        RejectReason reason,
        DateTime utcNow,
        CancellationToken cancellationToken = default);

    /// <summary>Withdraws a command an operator no longer wants, if a node has not already taken it.</summary>
    Task<bool> CancelAsync(string commandId, DateTime utcNow, CancellationToken cancellationToken = default);

    /// <summary>Marks every command that has waited too long. Returns how many.</summary>
    Task<long> ExpireStaleAsync(DateTime utcNow, CancellationToken cancellationToken = default);

    Task<IReadOnlyList<Command>> QueryAsync(
        string? serial, int limit, CancellationToken cancellationToken = default);

    Task<IReadOnlyList<Command>> GetOutstandingAsync(CancellationToken cancellationToken = default);

    Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default);
}
