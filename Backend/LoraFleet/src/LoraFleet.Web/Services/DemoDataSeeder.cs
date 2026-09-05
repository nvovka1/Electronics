using LoraFleet.DataAccess.Repositories;
using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Logs;

namespace LoraFleet.Web.Services;

/// <summary>
/// Fills an empty database with a plausible fleet, so the dashboard can be seen
/// working before any hardware exists.
/// </summary>
/// <remarks>
/// Only runs when SeedDemoData is true and the Devices collection is empty, so
/// it can never overwrite real check-ins. The serials are prefixed DEMO- to
/// make it obvious which rows are not a real node.
/// </remarks>
public sealed class DemoDataSeeder : IHostedService
{
    private readonly IServiceScopeFactory _scopes;
    private readonly IConfiguration _configuration;
    private readonly TimeProvider _clock;
    private readonly ILogger<DemoDataSeeder> _logger;

    public DemoDataSeeder(
        IServiceScopeFactory scopes,
        IConfiguration configuration,
        TimeProvider clock,
        ILogger<DemoDataSeeder> logger)
    {
        _scopes = scopes ?? throw new ArgumentNullException(nameof(scopes));
        _configuration = configuration ?? throw new ArgumentNullException(nameof(configuration));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        if (!_configuration.GetValue("SeedDemoData", false)) return;

        try
        {
            using var scope = _scopes.CreateScope();
            var devices = scope.ServiceProvider.GetRequiredService<IDeviceRepository>();
            var logs = scope.ServiceProvider.GetRequiredService<ILogRepository>();

            var existing = await devices.GetAllAsync(cancellationToken);
            if (existing.Count > 0)
            {
                _logger.LogInformation("Demo seed skipped: {Count} devices already enrolled", existing.Count);
                return;
            }

            var now = _clock.GetUtcNow().UtcDateTime;

            // Four nodes covering the four states worth looking at: fine, a
            // degraded block, a node that has gone quiet, and one running a
            // build nobody can reproduce.
            await SeedDeviceAsync(devices, logs, now, "DEMO-A41C", 1,
                silence: TimeSpan.FromSeconds(20), postMask: 0x0000,
                batteryDeciVolts: 39, reboots: 2, isDirty: false, target: null);

            await SeedDeviceAsync(devices, logs, now, "DEMO-B72E", 2,
                silence: TimeSpan.FromMinutes(1), postMask: 0x0004, // adc
                batteryDeciVolts: 0, reboots: 5, isDirty: false, target: "1.1.0");

            await SeedDeviceAsync(devices, logs, now, "DEMO-C118", 3,
                silence: TimeSpan.FromHours(6), postMask: 0x0008, // radio, critical
                batteryDeciVolts: 32, reboots: 41, isDirty: false, target: null);

            await SeedDeviceAsync(devices, logs, now, "DEMO-D9F0", 4,
                silence: TimeSpan.FromSeconds(45), postMask: 0x0000,
                batteryDeciVolts: 41, reboots: 1, isDirty: true, target: null);

            _logger.LogInformation("Seeded 4 demo devices. Set SeedDemoData=false once real nodes are reporting.");
        }
        catch (Exception exception)
        {
            // Never fatal. A seed failure must not stop the site coming up and
            // reporting why the database is unreachable.
            _logger.LogError(exception, "Demo seed failed");
        }
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    private static async Task SeedDeviceAsync(
        IDeviceRepository devices,
        ILogRepository logs,
        DateTime now,
        string serial,
        int nodeId,
        TimeSpan silence,
        int postMask,
        int batteryDeciVolts,
        int reboots,
        bool isDirty,
        string? target)
    {
        var lastSeen = now - silence;

        await devices.UpsertCheckInAsync(new Device
        {
            Serial = serial,
            NodeId = nodeId,
            HardwareId = "ttgo-lora32-v21new",
            FirmwareVersion = isDirty ? "1.1.0" : "1.0.0",
            FirmwareHash = isDirty ? "a1b2c3d" : "59b0d70",
            FirmwareIsDirty = isDirty,
            BuildType = "field",
            ProtocolVersion = 1,
            ConfigVersion = 2,
            EnrolledAt = now.AddDays(-9),
            LastSeenAt = lastSeen,
            LastHealth = new DeviceHealth
            {
                ReceivedAt = lastSeen,
                UptimeSeconds = (long)silence.TotalSeconds + 86_400,
                Reboots = reboots,
                LastCrashCode = reboots > 20 ? 4 : 1, // PANIC when it keeps falling over
                LastCrashReason = LogDictionary.ResetReasonName(reboots > 20 ? 4 : 1),
                BatteryDeciVolts = batteryDeciVolts,
                LastRssi = -70 - (nodeId * 9),
                PostMask = postMask,
            },
        });

        if (target is not null) await devices.SetTargetFirmwareAsync(serial, target);

        var records = new List<LogRecord>();
        var uptimeMs = 86_400_000L;

        void Add(byte level, byte tag, byte code, long arg, int secondsAgo)
        {
            records.Add(new LogRecord
            {
                DeviceSerial = serial,
                TimestampMs = uptimeMs - (secondsAgo * 1000L),
                ReceivedAt = lastSeen.AddSeconds(-secondsAgo),
                Level = level,
                Tag = tag,
                Code = code,
                Arg = arg,
            });
        }

        Add(3, 0, 1, reboots > 20 ? 4 : 1, 3600);   // sys/boot
        Add(3, 2, 20, 16, 3598);                    // cfg/cfg_loaded, slot A seq 16
        Add(3, 1, 12, postMask, 3596);              // post/post_mask
        Add(3, 3, 38, 868_000, 3594);               // radio/radio_ready

        foreach (var block in PostBlock.Failing(postMask))
            Add(1, 1, 10, block.Bit, 3592);         // post/post_fail

        if (batteryDeciVolts == 0) Add(2, 6, 42, 0, 3400);          // batt/batt_untrusted
        if (batteryDeciVolts is > 0 and < 34) Add(2, 6, 41, batteryDeciVolts * 100, 3300); // batt_low

        Add(3, 3, 32, (2L << 16) | 41, 900);        // radio/rx
        Add(2, 3, 33, 42, 600);                     // radio/tx_noack
        Add(3, 3, 37, postMask, 300);               // radio/health_tx
        Add(3, 2, 23, (2L << 24) | 17, 120);        // cfg/cfg_changed, tx_power -> 17

        await logs.AppendAsync(records);
    }
}
