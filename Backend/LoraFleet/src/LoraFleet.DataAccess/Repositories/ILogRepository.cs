using LoraFleet.Domain.Logs;

namespace LoraFleet.DataAccess.Repositories;

/// <summary>How the caller wants the log narrowed down. Every field is optional.</summary>
public sealed record LogQuery
{
    public string? DeviceSerial { get; init; }

    /// <summary>Inclusive ceiling on the numeric level, so 2 means "WARN and above".</summary>
    public byte? MaxLevel { get; init; }

    public byte? Tag { get; init; }

    public DateTime? Since { get; init; }

    public int Limit { get; init; } = 200;
}

public interface ILogRepository
{
    Task AppendAsync(IReadOnlyCollection<LogRecord> records, CancellationToken cancellationToken = default);

    /// <summary>Newest first: the records before a fall are the valuable part, not the ones around it.</summary>
    Task<IReadOnlyList<LogRecord>> QueryAsync(LogQuery query, CancellationToken cancellationToken = default);

    Task<long> CountAsync(LogQuery query, CancellationToken cancellationToken = default);

    Task DeleteForDeviceAsync(string serial, CancellationToken cancellationToken = default);
}
