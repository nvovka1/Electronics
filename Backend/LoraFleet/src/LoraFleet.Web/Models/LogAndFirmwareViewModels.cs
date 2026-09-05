using LoraFleet.Domain.Firmware;
using LoraFleet.Domain.Logs;

namespace LoraFleet.Web.Models;

public sealed class LogsViewModel
{
    public required IReadOnlyList<LogRecord> Records { get; init; }

    public string? DeviceSerial { get; init; }

    /// <summary>Inclusive ceiling on severity: 2 means WARN and above. Null means everything.</summary>
    public byte? MaxLevel { get; init; }

    public byte? Tag { get; init; }

    public int Limit { get; init; }

    public required IReadOnlyList<string> KnownSerials { get; init; }

    /// <summary>Lower numbers are more severe, which is worth spelling out in the filter.</summary>
    public static readonly (byte Value, string Label)[] LevelFilters =
    [
        (0, "PANIC only"),
        (1, "ERROR and above"),
        (2, "WARN and above"),
        (3, "INFO and above"),
        (5, "Everything"),
    ];

    public static readonly (byte Value, string Label)[] TagFilters =
    [
        (0, "sys"), (1, "post"), (2, "cfg"), (3, "radio"),
        (4, "ui"), (5, "key"), (6, "batt"),
    ];
}

public sealed class FirmwareListViewModel
{
    public required IReadOnlyList<FirmwareRelease> Releases { get; init; }

    /// <summary>How many devices are currently assigned each release, so nothing in use is deleted by accident.</summary>
    public required IReadOnlyDictionary<string, int> AssignmentCounts { get; init; }

    public int AssignedCount(string version) =>
        AssignmentCounts.TryGetValue(version, out var count) ? count : 0;
}
