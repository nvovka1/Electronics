using LoraFleet.Domain.Logs;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Driver;

namespace LoraFleet.DataAccess;

/// <summary>
/// Creates the indexes the queries actually rely on, once at start-up.
/// </summary>
/// <remarks>
/// Index creation in Mongo is idempotent, so this runs on every boot rather
/// than being a migration somebody has to remember. A failure here is logged
/// and swallowed: an unreachable database at start-up must not stop the site
/// from coming up and reporting that the database is unreachable.
/// </remarks>
public sealed class MongoIndexInitializer : IHostedService
{
    private readonly FleetDbContext _context;
    private readonly MongoOptions _options;
    private readonly ILogger<MongoIndexInitializer> _logger;

    public MongoIndexInitializer(
        FleetDbContext context,
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
            var keys = Builders<LogRecord>.IndexKeys;

            await _context.Logs.Indexes.CreateManyAsync(
                [
                    // The device detail page: one node's records, newest first.
                    new CreateIndexModel<LogRecord>(
                        keys.Ascending(record => record.DeviceSerial)
                            .Descending(record => record.ReceivedAt),
                        new CreateIndexOptions { Name = "ix_log_device_received" }),

                    // The global log page, filtered to WARN and above.
                    new CreateIndexModel<LogRecord>(
                        keys.Ascending(record => record.Level)
                            .Descending(record => record.ReceivedAt),
                        new CreateIndexOptions { Name = "ix_log_level_received" }),

                    // Logs are the bulk of the data and the least valuable part
                    // once a problem is closed, so they expire on their own
                    // rather than growing until somebody notices.
                    new CreateIndexModel<LogRecord>(
                        keys.Ascending(record => record.ReceivedAt),
                        new CreateIndexOptions
                        {
                            Name = "ix_log_ttl",
                            ExpireAfter = TimeSpan.FromDays(Math.Max(1, _options.LogRetentionDays)),
                        }),
                ],
                cancellationToken);

            _logger.LogInformation(
                "Mongo indexes ready; log retention {RetentionDays} days", _options.LogRetentionDays);
        }
        catch (Exception exception)
        {
            _logger.LogError(
                exception,
                "Could not create Mongo indexes. The site will still start, but queries will be slow " +
                "and logs will not expire until this succeeds.");
        }
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
