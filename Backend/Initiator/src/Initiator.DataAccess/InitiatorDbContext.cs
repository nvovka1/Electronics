using Initiator.Domain.Commands;
using Initiator.Domain.Devices;
using Initiator.Domain.Logs;
using Initiator.Domain.States;
using Initiator.Domain.Telemetry;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Conventions;
using MongoDB.Bson.Serialization.IdGenerators;
using MongoDB.Bson.Serialization.Serializers;
using MongoDB.Driver;

namespace Initiator.DataAccess;

/// <summary>
/// Owns the Mongo client and the collection handles. Registered as a singleton:
/// the driver's client is already a pool, and creating one per request is the
/// classic way to exhaust connections against Atlas.
/// </summary>
public sealed class InitiatorDbContext
{
    private static int _conventionsRegistered;

    public InitiatorDbContext(IOptions<MongoOptions> options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var settings = options.Value;

        if (string.IsNullOrWhiteSpace(settings.ConnectionString))
        {
            throw new InvalidOperationException(
                "Mongo:ConnectionString is not configured. Set MONGO__CONNECTIONSTRING in the " +
                "environment (see .env.example) or use dotnet user-secrets locally.");
        }

        RegisterConventions();

        var client = new MongoClient(settings.ConnectionString);
        Database = client.GetDatabase(settings.DatabaseName);

        Devices = Database.GetCollection<Device>("Devices");
        Commands = Database.GetCollection<Command>("Commands");
        StateEvents = Database.GetCollection<StateEvent>("StateEvents");
        Logs = Database.GetCollection<LogRecord>("DeviceLogs");
        Flights = Database.GetCollection<Flight>("Flights");
        TelemetrySamples = Database.GetCollection<TelemetrySample>("TelemetrySamples");
    }

    public IMongoDatabase Database { get; }

    public IMongoCollection<Device> Devices { get; }

    public IMongoCollection<Command> Commands { get; }

    public IMongoCollection<StateEvent> StateEvents { get; }

    public IMongoCollection<LogRecord> Logs { get; }

    public IMongoCollection<Flight> Flights { get; }

    /// <summary>
    /// By far the largest collection: one document per row per flight. Kept
    /// separate from the summaries in <see cref="Flights"/> so the list page
    /// never touches it.
    /// </summary>
    public IMongoCollection<TelemetrySample> TelemetrySamples { get; }

    /// <summary>
    /// Conventions are process-wide in the driver and registering the same pack
    /// twice throws. The guard matters because the tests build several contexts.
    /// </summary>
    private static void RegisterConventions()
    {
        if (Interlocked.Exchange(ref _conventionsRegistered, 1) == 1) return;

        ConventionRegistry.Register(
            "initiator",
            new ConventionPack
            {
                new CamelCaseElementNameConvention(),
                new IgnoreExtraElementsConvention(true),

                // States and commands are stored by name, not by number. The
                // numbers are a wire protocol and will outlive this database;
                // a stored "Armed" stays readable if a code is ever renumbered,
                // and is legible to anyone querying the collection by hand.
                new EnumRepresentationConvention(BsonType.String),
            },
            _ => true);

        BsonClassMap.RegisterClassMap<Device>(map =>
        {
            map.AutoMap();

            // The serial is printed on the case and travels in every report, so
            // it is already a natural key. A generated ObjectId would only add a
            // second identity for the same thing.
            map.MapIdMember(device => device.Serial);
            map.UnmapMember(device => device.AutoArmDueAt);
        });

        BsonClassMap.RegisterClassMap<DeviceHealth>(map =>
        {
            map.AutoMap();
            map.UnmapMember(health => health.BatteryIsTrusted);
            map.UnmapMember(health => health.BatteryVolts);
            map.UnmapMember(health => health.PostPassed);
        });

        BsonClassMap.RegisterClassMap<Command>(map =>
        {
            map.AutoMap();
            MapStringObjectId(map, command => command.Id);
            map.UnmapMember(command => command.IsTerminal);
            map.UnmapMember(command => command.IsOutstanding);
        });

        BsonClassMap.RegisterClassMap<StateEvent>(map =>
        {
            map.AutoMap();
            MapStringObjectId(map, stateEvent => stateEvent.Id);
            map.UnmapMember(stateEvent => stateEvent.StateChanged);
            map.UnmapMember(stateEvent => stateEvent.IsAutoArm);
        });

        BsonClassMap.RegisterClassMap<LogRecord>(map =>
        {
            map.AutoMap();
            MapStringObjectId(map, record => record.Id);

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

        BsonClassMap.RegisterClassMap<Flight>(map =>
        {
            map.AutoMap();

            // serial:flightId, a natural key like the device serial. A generated
            // id would add a second identity for the same thing and turn every
            // ingest upsert into a lookup followed by a write.
            map.MapIdMember(flight => flight.Id);
        });

        BsonClassMap.RegisterClassMap<TelemetrySample>(map =>
        {
            map.AutoMap();
            MapStringObjectId(map, sample => sample.Id);

            // Nulls are NOT stored. A row of a flight without a GPS fix is
            // mostly nulls, and at tens of thousands of rows a flight the
            // difference is the free tier holding a season or a fortnight.
            // Reading is unaffected: an absent element deserialises to null,
            // which is what it means.
            map.SetIgnoreExtraElements(true);
            foreach (var member in map.DeclaredMemberMaps)
            {
                member.SetIgnoreIfNull(true);
            }
        });
    }

    /// <summary>
    /// A string property that Mongo should fill with a generated ObjectId. Kept
    /// as a string in the domain so that nothing outside this project has to
    /// reference the driver.
    /// </summary>
    private static void MapStringObjectId<T>(
        BsonClassMap<T> map, System.Linq.Expressions.Expression<Func<T, string>> member)
    {
        map.MapIdMember(member)
           .SetIdGenerator(StringObjectIdGenerator.Instance)
           .SetSerializer(new StringSerializer(BsonType.ObjectId));
    }
}
