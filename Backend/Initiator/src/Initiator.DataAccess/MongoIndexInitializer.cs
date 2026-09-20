using Initiator.Domain.Commands;
using Initiator.Domain.Logs;
using Initiator.Domain.States;
using Initiator.Domain.Telemetry;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Driver;

namespace Initiator.DataAccess;

/// <summary>
/// Creates the indexes the queries actually rely on, once at start-up.
/// </summary>
/// <remarks>
/// Index creation in Mongo is idempotent, so this runs on every boot rather
/// than being a migration somebody has to remember. A failure is logged and
/// swallowed: an unreachable database at start-up must not stop the site from
/// coming up and reporting that the database is unreachable.
/// </remarks>
public sealed class MongoIndexInitializer : IHostedService
{
    private readonly InitiatorDbContext _context;
    private readonly MongoOptions _options;
    private readonly ILogger<MongoIndexInitializer> _logger;

    public MongoIndexInitializer(
        InitiatorDbContext context,
        IOptions<MongoOptions> options,
        ILogger<MongoIndexInitializer> logger)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        try
        {
            await CreateCommandIndexesAsync(cancellationToken);
            await CreateStateEventIndexesAsync(cancellationToken);
            await CreateLogIndexesAsync(cancellationToken);
            await CreateTelemetryIndexesAsync(cancellationToken);

            _logger.LogInformation(
                "Mongo indexes ready; logs kept {LogDays} days, state events {EventDays} days",
                _options.LogRetentionDays,
                _options.StateEventRetentionDays);
        }
        catch (Exception exception)
        {
            _logger.LogError(
                exception,
                "Could not create Mongo indexes. The site will still start, but queries will be " +
                "slow and nothing will expire until this succeeds.");
        }
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    private Task CreateCommandIndexesAsync(CancellationToken cancellationToken)
    {
        var keys = Builders<Command>.IndexKeys;

        return _context.Commands.Indexes.CreateManyAsync(
            [
                // The hot path: every device poll runs this, so it is the one
                // index whose absence would be felt immediately.
                new CreateIndexModel<Command>(
                    keys.Ascending(command => command.DeviceSerial)
                        .Ascending(command => command.Status)
                        .Ascending(command => command.QueuedAt),
                    new CreateIndexOptions { Name = "ix_command_device_status_queued" }),

                // The expiry sweep, which also runs on every poll.
                new CreateIndexModel<Command>(
                    keys.Ascending(command => command.Status)
                        .Ascending(command => command.QueuedAt),
                    new CreateIndexOptions { Name = "ix_command_status_queued" }),
            ],
            cancellationToken);
    }

    private Task CreateStateEventIndexesAsync(CancellationToken cancellationToken)
    {
        var keys = Builders<StateEvent>.IndexKeys;

        return _context.StateEvents.Indexes.CreateManyAsync(
            [
                new CreateIndexModel<StateEvent>(
                    keys.Ascending(stateEvent => stateEvent.DeviceSerial)
                        .Descending(stateEvent => stateEvent.ReceivedAt),
                    new CreateIndexOptions { Name = "ix_state_event_device_received" }),

                // Kept far longer than logs: this is the record of what the
                // nodes actually did, which is the thing anyone will want
                // afterwards.
                new CreateIndexModel<StateEvent>(
                    keys.Ascending(stateEvent => stateEvent.ReceivedAt),
                    new CreateIndexOptions
                    {
                        Name = "ix_state_event_ttl",
                        ExpireAfter = TimeSpan.FromDays(Math.Max(1, _options.StateEventRetentionDays)),
                    }),
            ],
            cancellationToken);
    }

    /// <summary>
    /// The unique index here is not an optimisation - it is the idempotency
    /// mechanism. The board re-sends any batch whose acknowledgement went
    /// missing, which over a free instance that sleeps is routine, and this is
    /// what makes a repeat free instead of a duplicated row.
    /// </summary>
    private async Task CreateTelemetryIndexesAsync(CancellationToken cancellationToken)
    {
        var sampleKeys = Builders<TelemetrySample>.IndexKeys;

        await _context.TelemetrySamples.Indexes.CreateManyAsync(
            [
                new CreateIndexModel<TelemetrySample>(
                    sampleKeys.Ascending(sample => sample.Serial)
                              .Ascending(sample => sample.FlightId)
                              .Ascending(sample => sample.Index),
                    new CreateIndexOptions
                    {
                        Name = "ux_sample_serial_flight_index",
                        Unique = true,
                    }),
            ],
            cancellationToken);

        var flightKeys = Builders<Flight>.IndexKeys;

        await _context.Flights.Indexes.CreateManyAsync(
            [
                new CreateIndexModel<Flight>(
                    flightKeys.Descending(flight => flight.LastSampleAt),
                    new CreateIndexOptions { Name = "ix_flight_last_sample" }),

                new CreateIndexModel<Flight>(
                    flightKeys.Ascending(flight => flight.Serial)
                              .Descending(flight => flight.FlightId),
                    new CreateIndexOptions { Name = "ix_flight_serial_id" }),
            ],
            cancellationToken);
    }

    private Task CreateLogIndexesAsync(CancellationToken cancellationToken)
    {
        var keys = Builders<LogRecord>.IndexKeys;

        return _context.Logs.Indexes.CreateManyAsync(
            [
                new CreateIndexModel<LogRecord>(
                    keys.Ascending(record => record.DeviceSerial)
                        .Descending(record => record.ReceivedAt),
                    new CreateIndexOptions { Name = "ix_log_device_received" }),

                new CreateIndexModel<LogRecord>(
                    keys.Ascending(record => record.Level)
                        .Descending(record => record.ReceivedAt),
                    new CreateIndexOptions { Name = "ix_log_level_received" }),

                new CreateIndexModel<LogRecord>(
                    keys.Ascending(record => record.ReceivedAt),
                    new CreateIndexOptions
                    {
                        Name = "ix_log_ttl",
                        ExpireAfter = TimeSpan.FromDays(Math.Max(1, _options.LogRetentionDays)),
                    }),
            ],
            cancellationToken);
    }
}
