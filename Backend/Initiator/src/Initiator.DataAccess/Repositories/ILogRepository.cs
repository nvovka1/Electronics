using Initiator.Domain.Logs;

namespace Initiator.DataAccess.Repositories;

/// <summary>What the log page is asking for. Every field is optional.</summary>
public sealed class LogQuery
{
    public string? DeviceSerial { get; set; }

    /// <summary>
    /// Keep records at this level and worse. Lower numbers are more severe, so
    /// "at most" is the natural way to ask for "this and worse".
    /// </summary>
    public byte? MaxLevel { get; set; }

    public byte? Tag { get; set; }

    public DateTime? Since { get; set; }

    public int Limit { get; set; } = 200;
}

public interface ILogRepository
{
    Task AppendAsync(
        IReadOnlyCollection<LogRecord> records, CancellationToken cancellationToken = default);

    Task<IReadOnlyList<LogRecord>> QueryAsync(
        LogQuery query, CancellationToken cancellationToken = default);

    Task<long> CountAsync(LogQuery query, CancellationToken cancellationToken = default);

    Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default);
}
