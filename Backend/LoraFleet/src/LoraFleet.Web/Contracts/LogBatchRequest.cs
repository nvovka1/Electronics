using System.ComponentModel.DataAnnotations;

namespace LoraFleet.Web.Contracts;

/// <summary>
/// A batch of ring-log records. Nodes send codes, not sentences, so a record is
/// four small numbers and the text is reconstructed on this side.
/// </summary>
public sealed class LogBatchRequest
{
    /// <summary>
    /// Capped because a node's ring holds 256 records and anything larger is
    /// either a bug or somebody else pointing a load generator at us.
    /// </summary>
    [MaxLength(512)]
    public List<LogEntryRequest> Records { get; set; } = [];
}

public sealed class LogEntryRequest
{
    /// <summary>Milliseconds since that node booted. Not comparable across a reboot, and never wall-clock.</summary>
    [Range(0, long.MaxValue)]
    public long TimestampMs { get; set; }

    [Range(0, 5)]
    public byte Level { get; set; }

    [Range(0, 255)]
    public byte Tag { get; set; }

    [Range(0, 255)]
    public byte Code { get; set; }

    public long Arg { get; set; }
}
