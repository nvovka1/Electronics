using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Firmware;
using LoraFleet.Domain.Logs;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Conventions;
using MongoDB.Driver;
using MongoDB.Driver.GridFS;

namespace LoraFleet.DataAccess;

/// <summary>
/// Owns the Mongo client and the collection handles. Registered as a singleton:
/// the driver's client is already a pool and creating one per request is the
/// classic way to exhaust connections against Atlas.
/// </summary>
public sealed class FleetDbContext
{
    private static int _conventionsRegistered;

    public FleetDbContext(IOptions<MongoOptions> options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var settings = options.Value;

        if (string.IsNullOrWhiteSpace(settings.ConnectionString))
        {
            throw new InvalidOperationException(
                "Mongo:ConnectionString is not configured. Set MONGO_CONNECTION_STRING " +
                "in the environment (see .env.example) or use dotnet user-secrets locally.");
        }

        RegisterConventions();

        var client = new MongoClient(settings.ConnectionString);
        Database = client.GetDatabase(settings.DatabaseName);

        Devices = Database.GetCollection<Device>("Devices");
        Logs = Database.GetCollection<LogRecord>("DeviceLogs");
        FirmwareReleases = Database.GetCollection<FirmwareRelease>("FirmwareReleases");
        FirmwareBinaries = new GridFSBucket(Database, new GridFSBucketOptions
        {
            BucketName = "firmware",
        });
    }

    public IMongoDatabase Database { get; }

    public IMongoCollection<Device> Devices { get; }

    public IMongoCollection<LogRecord> Logs { get; }

    public IMongoCollection<FirmwareRelease> FirmwareReleases { get; }

    /// <summary>The .bin files themselves. A third of a megabyte each is past what belongs in a document.</summary>
    public GridFSBucket FirmwareBinaries { get; }

    /// <summary>
    /// Conventions are process-wide in the driver, and registering the same
    /// pack twice throws. The guard matters because tests build several
    /// contexts.
    /// </summary>
    private static void RegisterConventions()
    {
        if (Interlocked.Exchange(ref _conventionsRegistered, 1) == 1) return;

        // camelCase on the wire, PascalCase in C#, set once rather than
        // annotated on every property.
        ConventionRegistry.Register(
            "lorafleet",
            new ConventionPack
            {
                new CamelCaseElementNameConvention(),
                new IgnoreExtraElementsConvention(true),
                new EnumRepresentationConvention(BsonType.String),
            },
            _ => true);

        // The serial is printed on the case and travels in every frame, so it
        // is already a natural key. A generated ObjectId would only add a
        // second identity for the same thing.
        BsonClassMap.RegisterClassMap<Device>(map =>
        {
            map.AutoMap();
            map.MapIdMember(device => device.Serial);
            map.UnmapMember(device => device.IsOutOfDate);
        });

        BsonClassMap.RegisterClassMap<DeviceHealth>(map =>
        {
            map.AutoMap();
            map.UnmapMember(health => health.BatteryIsTrusted);
            map.UnmapMember(health => health.BatteryVolts);
            map.UnmapMember(health => health.PostPassed);
        });

        BsonClassMap.RegisterClassMap<LogRecord>(map =>
        {
            map.AutoMap();
            map.MapIdMember(record => record.Id)
               .SetIdGenerator(MongoDB.Bson.Serialization.IdGenerators.StringObjectIdGenerator.Instance)
               .SetSerializer(new MongoDB.Bson.Serialization.Serializers.StringSerializer(BsonType.ObjectId));

            // Everything below is derived from the dictionary at read time.
            // Storing it would duplicate the decoding and let a stored copy go
            // stale the moment a code's meaning is corrected.
            map.UnmapMember(record => record.LevelValue);
            map.UnmapMember(record => record.LevelName);
            map.UnmapMember(record => record.TagName);
            map.UnmapMember(record => record.CodeName);
            map.UnmapMember(record => record.ArgDescription);
            map.UnmapMember(record => record.IsNoteworthy);
        });

        BsonClassMap.RegisterClassMap<FirmwareRelease>(map =>
        {
            map.AutoMap();
            map.MapIdMember(release => release.Id);
            map.UnmapMember(release => release.HasBinary);
            map.UnmapMember(release => release.IsDeployable);
            map.UnmapMember(release => release.DisplayName);
        });
    }
}
