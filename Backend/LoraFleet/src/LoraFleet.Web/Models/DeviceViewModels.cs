using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Firmware;
using LoraFleet.Domain.Logs;

namespace LoraFleet.Web.Models;

/// <summary>One row of the fleet list: the device plus everything derived for display.</summary>
public sealed class DeviceRowViewModel
{
    public required Device Device { get; init; }

    public required DeviceStatus Status { get; init; }

    public required TimeSpan Silence { get; init; }

    public bool NeedsAttention => DeviceStatusPolicy.NeedsAttention(Status);

    public string PostSummary => PostBlock.Describe(Device.LastHealth?.PostMask ?? 0);

    /// <summary>"4 min ago". Absolute timestamps make an operator do arithmetic.</summary>
    public string SilenceDescription => Describe(Silence);

    public static string Describe(TimeSpan span)
    {
        if (span < TimeSpan.Zero) return "just now";
        if (span.TotalSeconds < 60) return $"{(int)span.TotalSeconds}s ago";
        if (span.TotalMinutes < 60) return $"{(int)span.TotalMinutes} min ago";
        if (span.TotalHours < 48) return $"{(int)span.TotalHours} h ago";
        return $"{(int)span.TotalDays} d ago";
    }
}

public sealed class DeviceListViewModel
{
    public required IReadOnlyList<DeviceRowViewModel> Devices { get; init; }

    public int Total => Devices.Count;

    public int Healthy => Devices.Count(row => row.Status == DeviceStatus.Healthy);

    public int NeedingAttention => Devices.Count(row => row.NeedsAttention);

    public int OutOfDate => Devices.Count(row => row.Device.IsOutOfDate);

    /// <summary>A build with uncommitted changes cannot be reproduced, so a node running one is worth surfacing on the overview.</summary>
    public int RunningDirtyBuilds => Devices.Count(row => row.Device.FirmwareIsDirty);
}

public sealed class DeviceDetailViewModel
{
    public required Device Device { get; init; }

    public required DeviceStatus Status { get; init; }

    public required TimeSpan Silence { get; init; }

    public required IReadOnlyList<LogRecord> RecentLogs { get; init; }

    /// <summary>Releases that could actually be assigned to this board. A dirty build is never among them.</summary>
    public required IReadOnlyList<FirmwareRelease> AssignableReleases { get; init; }

    public IEnumerable<PostBlock> FailingBlocks =>
        PostBlock.Failing(Device.LastHealth?.PostMask ?? 0);

    public bool HasCriticalFailure =>
        PostBlock.HasCriticalFailure(Device.LastHealth?.PostMask ?? 0);

    public string SilenceDescription => DeviceRowViewModel.Describe(Silence);
}
