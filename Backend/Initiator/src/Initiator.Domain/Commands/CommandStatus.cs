namespace Initiator.Domain.Commands;

/// <summary>
/// Where a dashboard-queued command has got to. Commands from the handheld
/// controller never appear here — they go straight over the radio and are only
/// ever seen afterwards, as state events.
/// </summary>
public enum CommandStatus
{
    /// <summary>Queued. The node has not collected it yet.</summary>
    Pending,

    /// <summary>The node has fetched it. Whether it acted on it is not yet known.</summary>
    Delivered,

    /// <summary>The node reported the transition.</summary>
    Applied,

    /// <summary>The node refused it — almost always because its real state was not what the dashboard believed.</summary>
    Rejected,

    /// <summary>
    /// Sat in the queue too long and will not be delivered. A node that has been
    /// offline for an hour should not come back and act on an INIT somebody
    /// clicked at the start of it.
    /// </summary>
    Expired,

    /// <summary>Withdrawn from the queue by an operator before the node collected it.</summary>
    Cancelled,
}
