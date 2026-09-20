using Initiator.DataAccess;
using Initiator.DataAccess.Repositories;
using Initiator.Web.Contracts;
using Initiator.Web.Services;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

namespace Initiator.Tests;

/// <summary>
/// Ingest against a real mongod, because the part that is easy to get wrong is
/// the unique index doing the de-duplication — and a mock index agrees with
/// whatever the code happens to do.
/// </summary>
[Collection(MongoCollection.Name)]
public sealed class TelemetryIngestServiceTests : IAsyncLifetime
{
    private const string Serial = "uav-a41c";

    private readonly InitiatorDbContext _context;
    private readonly MongoTelemetrySampleRepository _samples;
    private readonly MongoFlightRepository _flights;
    private readonly MongoOptions _options = new() { FlightsKeptPerAircraft = 3 };

    public TelemetryIngestServiceTests(MongoFixture mongo)
    {
        // A database per test class, so one class's leftovers cannot decide
        // another's result.
        _context = mongo.CreateContext($"telemetry-{Guid.NewGuid():N}");
        _samples = new MongoTelemetrySampleRepository(_context);
        _flights = new MongoFlightRepository(_context);
    }

    /// <summary>
    /// The indexes are created by the hosted service in the real host, and the
    /// unique one is not an optimisation here — it is the de-duplication. So the
    /// initializer runs, rather than the test creating an index of its own that
    /// could differ from the one production gets.
    /// </summary>
    public async Task InitializeAsync()
    {
        var initializer = new MongoIndexInitializer(
            _context, Options.Create(_options), NullLogger<MongoIndexInitializer>.Instance);

        await initializer.StartAsync(CancellationToken.None);
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private TelemetryIngestService CreateService() => new(
        _samples, _flights, Options.Create(_options), TimeProvider.System,
        NullLogger<TelemetryIngestService>.Instance);

    private static TelemetryBatchRequest Batch(int flightId, int firstIndex, int count) => new()
    {
        FlightId = flightId,
        FirstIndex = firstIndex,
        Samples = Enumerable.Range(firstIndex, count).Select(index => new TelemetrySampleDto
        {
            Index = index,
            TMs = index * 500,
            Utc = new DateTime(2026, 9, 20, 12, 0, 0, DateTimeKind.Utc).AddMilliseconds(index * 500),
            Armed = 1,
            AltRel = index,
            GroundSpeed = index * 0.5,
            BatteryVoltage = 16.8 - (index * 0.01),
        }).ToList(),
    };

    [Fact]
    public async Task A_first_batch_creates_the_flight_and_enrols_the_aircraft()
    {
        var result = await CreateService().IngestAsync(Serial, Batch(1, 0, 10));

        Assert.Equal(1, result.FlightId);
        Assert.Equal(10, result.Stored);
        Assert.Equal(10, result.NextIndex);

        var aircraft = await _flights.ListAircraftAsync();
        Assert.Equal(Serial, Assert.Single(aircraft).Serial);
    }

    [Fact]
    public async Task Re_sending_a_batch_stores_nothing_and_still_succeeds()
    {
        var service = CreateService();
        await service.IngestAsync(Serial, Batch(2, 0, 10));

        // What happens every time an acknowledgement is lost, which over a free
        // instance that sleeps is routine rather than exceptional.
        var repeat = await service.IngestAsync(Serial, Batch(2, 0, 10));

        Assert.Equal(0, repeat.Stored);
        Assert.Equal(10, repeat.NextIndex);

        var flight = await _flights.GetAsync(Serial, 2);
        Assert.Equal(10, flight!.SampleCount);
    }

    [Fact]
    public async Task An_overlapping_batch_keeps_only_the_rows_that_are_new()
    {
        var service = CreateService();
        await service.IngestAsync(Serial, Batch(3, 0, 10));

        // A rewind: the board went back and re-sent from row five.
        var overlap = await service.IngestAsync(Serial, Batch(3, 5, 10));

        Assert.Equal(5, overlap.Stored);
        Assert.Equal(15, overlap.NextIndex);
    }

    [Fact]
    public async Task The_next_index_comes_from_what_is_stored_not_from_the_request()
    {
        var service = CreateService();
        await service.IngestAsync(Serial, Batch(4, 0, 10));

        // A batch claiming to start at 500 while carrying rows 0-9 again. The
        // board sets its cursor from the answer, so an answer that believed the
        // request would strand every row in between.
        var lying = Batch(4, 0, 10);
        lying.FirstIndex = 500;

        var result = await service.IngestAsync(Serial, lying);

        Assert.Equal(10, result.NextIndex);
    }

    [Fact]
    public async Task The_flight_summary_is_built_from_the_rows()
    {
        await CreateService().IngestAsync(Serial, Batch(5, 0, 10));

        var flight = await _flights.GetAsync(Serial, 5);

        Assert.NotNull(flight);
        Assert.Equal(new DateTime(2026, 9, 20, 12, 0, 0, DateTimeKind.Utc), flight!.StartedAt);
        Assert.Equal(9, flight.MaxAltRel);
        Assert.Equal(4.5, flight.MaxGroundSpeed);
        Assert.Equal(16.71, flight.MinBatteryVoltage!.Value, 2);
        Assert.Equal(4500, flight.DurationMs);
    }

    [Fact]
    public async Task An_unreported_value_stays_unreported()
    {
        var batch = new TelemetryBatchRequest
        {
            FlightId = 6,
            FirstIndex = 0,
            Samples =
            [
                new TelemetrySampleDto { Index = 0, TMs = 0 },
            ],
        };

        await CreateService().IngestAsync(Serial, batch);

        var stored = Assert.Single(await _samples.PageAsync(Serial, 6, 0, 10));

        // Null all the way through. Anything that turned these into zeros would
        // make a flight with no GPS indistinguishable from one sitting on the
        // equator with a flat battery.
        Assert.Null(stored.Lat);
        Assert.Null(stored.BatteryVoltage);
        Assert.Null(stored.Armed);
        Assert.Null(stored.Utc);
    }

    [Fact]
    public async Task Only_the_newest_flights_are_kept()
    {
        var service = CreateService();

        for (var flightId = 1; flightId <= 5; flightId++)
        {
            await service.IngestAsync(Serial, Batch(flightId, 0, 3));
        }

        var flights = await _flights.ListAsync(Serial, 100);

        // Three kept, oldest two dropped along with their rows.
        Assert.Equal(3, flights.Count);
        Assert.Equal([5, 4, 3], flights.Select(flight => flight.FlightId).OrderByDescending(id => id));
        Assert.Empty(await _samples.PageAsync(Serial, 1, 0, 10));
    }
}
