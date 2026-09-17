namespace Initiator.Web.Contracts;

/// <summary>Records out of a node's ring log.</summary>
public sealed class LogBatchRequest
{
    public List<LogRecordRequest> Records { get; set; } = [];
}

public sealed class LogRecordRequest
{
    /// <summary>The node's own monotonic clock — milliseconds since it booted.</summary>
    public long TimestampMs { get; set; }

    public byte Level { get; set; }

    public byte Tag { get; set; }

    public byte Code { get; set; }

    /// <summary>
    /// The code's argument, meaning whatever that code documents. Some codes
    /// pack two values in here; <c>LogDictionary</c> knows which.
    /// </summary>
    public long Arg { get; set; }
}
