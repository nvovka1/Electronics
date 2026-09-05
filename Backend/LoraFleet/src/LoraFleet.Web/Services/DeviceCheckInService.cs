using LoraFleet.DataAccess.Repositories;
using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Logs;
using LoraFleet.Web.Contracts;

namespace LoraFleet.Web.Services;

public interface IDeviceCheckInService
{
    Task<Device> RecordCheckInAsync(
        string serial, CheckInRequest request, CancellationToken cancellationToken = default);

    Task<int> RecordLogsAsync(
        string serial, LogBatchRequest request, CancellationToken cancellationToken = default);
}

/// <summary>
/// The write path from the fleet: a node reporting on itself, and a node
/// handing over a slice of its ring log.
/// </summary>
public sealed class DeviceCheckInService : IDeviceCheckInService
{
    private readonly IDeviceRepository _devices;
    private readonly ILogRepository _logs;
    private readonly TimeProvider _clock;
    private readonly ILogger<DeviceCheckInService> _logger;

    public DeviceCheckInService(
        IDeviceRepository devices,
        ILogRepository logs,
        TimeProvider clock,
        ILogger<DeviceCheckInService> logger)
    {
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
        _logs = logs ?? throw new ArgumentNullException(nameof(logs));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task<Device> RecordCheckInAsync(
        string serial, CheckInRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(serial);
        ArgumentNullException.ThrowIfNull(request);

        var now = _clock.GetUtcNow().UtcDateTime;

        var device = new Device
        {
            Serial = serial,
            NodeId = request.NodeId,
            HardwareId = request.HardwareId,
            FirmwareVersion = request.FirmwareVersion,
            FirmwareHash = request.FirmwareHash,
            FirmwareIsDirty = request.FirmwareIsDirty,
            BuildType = request.BuildType,
            ProtocolVersion = request.ProtocolVersion,
            ConfigVersion = request.ConfigVersion,
            LastSeenAt = now,
            EnrolledAt = now,
            LastHealth = new DeviceHealth
            {
                ReceivedAt = now,
                UptimeSeconds = request.UptimeSeconds,
                Reboots = request.Reboots,
                LastCrashCode = request.LastCrashCode,
                LastCrashReason = LogDictionary.ResetReasonName(request.LastCrashCode),
                BatteryDeciVolts = request.BatteryDeciVolts,
                LastRssi = request.LastRssi,
                PostMask = request.PostMask,
            },
        };

        var stored = await _devices.UpsertCheckInAsync(device, cancellationToken);

        // Worth a line in our own log, because these are the two states an
        // operator would otherwise only discover by opening the device page.
        if (request.PostMask != 0)
        {
            _logger.LogWarning(
                "Device {Serial} reported POST 0x{PostMask:X4} ({Blocks})",
                serial, request.PostMask, PostBlock.Describe(request.PostMask));
        }

        if (request.FirmwareIsDirty)
        {
            _logger.LogWarning(
                "Device {Serial} is running a dirty build ({Version}+{Hash}); there is no commit to reproduce it from",
                serial, request.FirmwareVersion, request.FirmwareHash);
        }

        return stored;
    }

    public async Task<int> RecordLogsAsync(
        string serial, LogBatchRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(serial);
        ArgumentNullException.ThrowIfNull(request);

        if (request.Records.Count == 0) return 0;

        var now = _clock.GetUtcNow().UtcDateTime;

        var records = request.Records
            .Select(entry => new LogRecord
            {
                DeviceSerial = serial,
                TimestampMs = entry.TimestampMs,
                ReceivedAt = now,
                Level = entry.Level,
                Tag = entry.Tag,
                Code = entry.Code,
                Arg = entry.Arg,
            })
            .ToList();

        await _logs.AppendAsync(records, cancellationToken);

        // A check-in is not required to accompany a log batch, but a node that
        // sent us something is demonstrably alive, so its silence timer resets.
        var existing = await _devices.GetBySerialAsync(serial, cancellationToken);
        if (existing is not null)
        {
            existing.LastSeenAt = now;
            await _devices.UpsertCheckInAsync(existing, cancellationToken);
        }

        return records.Count;
    }
}
