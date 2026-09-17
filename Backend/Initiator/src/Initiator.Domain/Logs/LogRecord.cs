namespace Initiator.Domain.Logs;

/// <summary>
/// One record out of a node's ring log. The device stores a code, not a
/// sentence: the same fact as formatted text costs about five times the bytes
/// and cannot be filtered without a regular expression. The text is
/// reconstructed here, from <see cref="LogDictionary"/>.
/// </summary>
public sealed class LogRecord
{
    public string Id { get; set; } = string.Empty;

    public required string DeviceSerial { get; set; }

    /// <summary>
    /// The device's own monotonic clock: milliseconds since that node booted.
    /// Nodes have no RTC, so this is never wall-clock time and never comparable
    /// across a reboot.
    /// </summary>
    public long TimestampMs { get; set; }

    /// <summary>When this service received it. The only absolute time in the record.</summary>
    public DateTime ReceivedAt { get; set; }

    public byte Level { get; set; }

    public byte Tag { get; set; }

    public byte Code { get; set; }

    public long Arg { get; set; }

    public LogLevel LevelValue => (LogLevel)Level;

    public string LevelName => LogDictionary.LevelName(Level);

    public string TagName => LogDictionary.TagName(Tag);

    public string CodeName => LogDictionary.CodeName(Code);

    /// <summary>The argument rendered per that code's documented meaning.</summary>
    public string ArgDescription => LogDictionary.DescribeArg(Code, Arg);

    /// <summary>WARN and above is what an operator filters to first.</summary>
    public bool IsNoteworthy => Level <= (byte)LogLevel.Warn;
}
