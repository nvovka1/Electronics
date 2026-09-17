using Initiator.Domain.Commands;
using Initiator.Domain.Devices;
using Initiator.Domain.Logs;
using Initiator.Domain.States;

namespace Initiator.Web.Models;

/// <summary>One row of the device list.</summary>
public sealed class DeviceRow
{
    public required Device Device { get; init; }

    public required DeviceStatus Status { get; init; }

    /// <summary>How long the node has been quiet, for the "last seen" column.</summary>
    public required TimeSpan Silence { get; init; }

    public bool NeedsAttention => DeviceStatusPolicy.NeedsAttention(Status);

    /// <summary>Not in SAFE. Worth the operator's eye even when nothing is wrong.</summary>
    public bool IsLive => DeviceStatusPolicy.IsLive(Device);
}

public sealed class DeviceListViewModel
{
    public required IReadOnlyList<DeviceRow> Devices { get; init; }

    /// <summary>
    /// Shown at the top, because "how many of my nodes are not in SAFE right
    /// now" is the question this page exists to answer at a glance.
    /// </summary>
    public int LiveCount => Devices.Count(row => row.IsLive);

    public int AttentionCount => Devices.Count(row => row.NeedsAttention);
}

public sealed class DeviceDetailsViewModel
{
    public required Device Device { get; init; }

    public required DeviceStatus Status { get; init; }

    /// <summary>
    /// The commands this node's last known state would accept. Anything else is
    /// rendered disabled rather than hidden, so the sequence is always visible
    /// as a whole.
    /// </summary>
    public required IReadOnlyList<CommandType> AllowedCommands { get; init; }

    public required IReadOnlyList<StateEvent> RecentEvents { get; init; }

    public required IReadOnlyList<Command> RecentCommands { get; init; }

    public required IReadOnlyList<LogRecord> RecentLogs { get; init; }

    /// <summary>
    /// Seconds until this node is expected to arm itself, or null when no
    /// countdown is running. <b>An estimate</b> — this service's arithmetic over
    /// the last report it received, not a clock the node is obeying.
    /// </summary>
    public int? AutoArmInSeconds { get; init; }

    public bool CanQueue(CommandType command) => AllowedCommands.Contains(command);
}
