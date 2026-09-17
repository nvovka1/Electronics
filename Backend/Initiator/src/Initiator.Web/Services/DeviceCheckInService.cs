using Initiator.DataAccess.Repositories;
using Initiator.Domain.Devices;
using Initiator.Domain.States;
using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;

namespace Initiator.Web.Services;

public sealed class DeviceCheckInService : IDeviceCheckInService
{
    private readonly IDeviceRepository _devices;
    private readonly TimeProvider _clock;
    private readonly ILogger<DeviceCheckInService> _logger;

    public DeviceCheckInService(
        IDeviceRepository devices, TimeProvider clock, ILogger<DeviceCheckInService> logger)
    {
        _devices = devices ?? throw new ArgumentNullException(nameof(devices));
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
            AutoArmTimeoutSeconds = request.AutoArmTimeoutSeconds,
            LastSeenAt = now,
            LastHealth = new DeviceHealth
            {
                ReceivedAt = now,
                UptimeSeconds = request.UptimeSeconds,
                Reboots = request.Reboots,
                LastCrashCode = request.LastCrashCode,
                BatteryDeciVolts = request.BatteryDeciVolts,
                LastRssi = request.LastRssi,
                PostMask = request.PostMask,
                LoraLinkUp = request.LoraLinkUp,
            },
        };

        var stored = await _devices.UpsertCheckInAsync(device, cancellationToken);

        // The reported state goes through its own guarded write, so a check-in
        // that was in flight while the node moved on cannot put the dashboard
        // back to where the node used to be.
        if (WireValues.TryState(request.State, out var state))
        {
            stored = await _devices.ApplyReportedStateAsync(
                serial,
                state,
                // A check-in says what the state is, not what put it there. The
                // event that caused it carries that, and arrives separately.
                stored.StateSource,
                request.Reboots,
                request.StateTimestampMs,
                now,
                cancellationToken) ?? stored;
        }
        else
        {
            // Worth a line rather than a silent ignore: it means the firmware
            // and this build disagree about the protocol, which is the kind of
            // thing that otherwise gets found weeks later.
            _logger.LogWarning(
                "Device {Serial} reported unknown state {State}; state not updated",
                serial, request.State);
        }

        return stored;
    }
}
