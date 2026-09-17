using Initiator.DataAccess.Repositories;
using Initiator.Domain.Devices;
using Initiator.Domain.States;
using Initiator.Web.Infrastructure;
using Microsoft.Extensions.Options;

namespace Initiator.Web.Services;

/// <summary>
/// Fills an empty database with a few plausible nodes so the dashboard can be
/// seen working before any hardware exists.
/// </summary>
/// <remarks>
/// Only runs when <c>Initiator:SeedDemoData</c> is on <b>and</b> no devices are
/// enrolled, so it can never overwrite a real fleet. The second condition is the
/// one that matters: the setting is easy to leave switched on by accident, and
/// the empty check makes that harmless.
/// </remarks>
public sealed class DemoDataSeeder : IHostedService
{
    private readonly IServiceScopeFactory _scopes;
    private readonly InitiatorOptions _options;
    private readonly TimeProvider _clock;
    private readonly ILogger<DemoDataSeeder> _logger;

    public DemoDataSeeder(
        IServiceScopeFactory scopes,
        IOptions<InitiatorOptions> options,
        TimeProvider clock,
        ILogger<DemoDataSeeder> logger)
    {
        _scopes = scopes ?? throw new ArgumentNullException(nameof(scopes));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        if (!_options.SeedDemoData) return;

        try
        {
            using var scope = _scopes.CreateScope();
            var devices = scope.ServiceProvider.GetRequiredService<IDeviceRepository>();

            var existing = await devices.GetAllAsync(cancellationToken);
            if (existing.Count > 0) return;

            var now = _clock.GetUtcNow().UtcDateTime;

            foreach (var device in Build(now))
            {
                await devices.UpsertCheckInAsync(device, cancellationToken);

                await devices.ApplyReportedStateAsync(
                    device.Serial,
                    device.CurrentState,
                    device.StateSource,
                    device.StateBootCount,
                    device.StateTimestampMs,
                    device.StateChangedAt,
                    cancellationToken);
            }

            _logger.LogInformation("Seeded demo devices into an empty database");
        }
        catch (Exception exception)
        {
            // A seeding failure must not stop the site coming up: the site is
            // how you would find out what went wrong.
            _logger.LogError(exception, "Could not seed demo data");
        }
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <summary>
    /// One node per interesting case: a quiet healthy one, one part-way through
    /// a sequence with its countdown running, one armed, and one that has gone
    /// silent while not in SAFE — which is the row the list exists to surface.
    /// </summary>
    private static IEnumerable<Device> Build(DateTime now)
    {
        yield return Demo("IN-1A2B", 1, NodeState.Safe, CommandSource.Api, now, secondsAgo: 5);
        yield return Demo("IN-3C4D", 2, NodeState.Init, CommandSource.Lora, now, secondsAgo: 20);
        yield return Demo("IN-5E6F", 3, NodeState.Armed, CommandSource.Timer, now, secondsAgo: 12);
        yield return Demo("IN-7A8C", 4, NodeState.Init, CommandSource.Lora, now, secondsAgo: 2400);
    }

    private static Device Demo(
        string serial, int nodeId, NodeState state, CommandSource source, DateTime now, int secondsAgo)
    {
        var lastSeen = now.AddSeconds(-secondsAgo);

        return new Device
        {
            Serial = serial,
            NodeId = nodeId,
            HardwareId = "ttgo-lora32-v21",
            FirmwareVersion = "0.1.0",
            FirmwareHash = "demo",
            BuildType = "dev",
            ProtocolVersion = 1,
            AutoArmTimeoutSeconds = 300,
            EnrolledAt = now.AddDays(-3),
            LastSeenAt = lastSeen,
            CurrentState = state,
            StateSource = source,
            StateChangedAt = lastSeen,
            StateBootCount = 1,
            StateTimestampMs = secondsAgo * 1000L,
            LastHealth = new DeviceHealth
            {
                ReceivedAt = lastSeen,
                UptimeSeconds = 3600,
                Reboots = 1,
                BatteryDeciVolts = 39,
                LastRssi = -92,
                PostMask = 0,
                LoraLinkUp = true,
            },
        };
    }
}
