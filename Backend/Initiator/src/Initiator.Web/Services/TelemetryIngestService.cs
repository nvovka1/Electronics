using Initiator.DataAccess;
using Initiator.DataAccess.Repositories;
using Initiator.Domain.Telemetry;
using Initiator.Web.Contracts;
using Microsoft.Extensions.Options;

namespace Initiator.Web.Services;

public sealed class TelemetryIngestService : ITelemetryIngestService
{
    private readonly ITelemetrySampleRepository _samples;
    private readonly IFlightRepository _flights;
    private readonly MongoOptions _options;
    private readonly TimeProvider _time;
    private readonly ILogger<TelemetryIngestService> _logger;

    public TelemetryIngestService(
        ITelemetrySampleRepository samples,
        IFlightRepository flights,
        IOptions<MongoOptions> options,
        TimeProvider time,
        ILogger<TelemetryIngestService> logger)
    {
        _samples = samples ?? throw new ArgumentNullException(nameof(samples));
        _flights = flights ?? throw new ArgumentNullException(nameof(flights));
        _options = options?.Value ?? throw new ArgumentNullException(nameof(options));
        _time = time ?? throw new ArgumentNullException(nameof(time));
        _logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public async Task<TelemetryIngestResult> IngestAsync(
        string serial, TelemetryBatchRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);

        var now = _time.GetUtcNow().UtcDateTime;

        var samples = request.Samples
            .Where(dto => dto.Index >= 0)
            .Select(dto => ToSample(serial, request.FlightId, dto, now))
            .ToList();

        var stored = await _samples.AppendAsync(samples, cancellationToken);

        // Read back rather than computed from the batch. A batch that arrives
        // out of order after a rewind would otherwise move the cursor backwards,
        // and the board would re-send the same rows for ever.
        var nextIndex = await _samples.NextIndexAsync(serial, request.FlightId, cancellationToken);

        var flight = await _flights.RecordBatchAsync(
            serial, request.FlightId, samples, stored, nextIndex, now, cancellationToken);

        await TrimAsync(serial, cancellationToken);

        return new TelemetryIngestResult(flight.FlightId, flight.NextIndex, stored);
    }

    /// <summary>
    /// Holds an aircraft to the configured number of flights. Runs on the ingest
    /// path rather than on a timer so that a service nobody is watching cannot
    /// quietly fill the free tier between visits.
    /// </summary>
    private async Task TrimAsync(string serial, CancellationToken cancellationToken)
    {
        var surplus = await _flights.ListBeyondAsync(
            serial, _options.FlightsKeptPerAircraft, cancellationToken);

        foreach (var flight in surplus)
        {
            // Rows first. A flight document removed while its rows survive would
            // leave them unreachable and uncountable - the worst of both.
            var removed = await _samples.DeleteForFlightAsync(
                serial, flight.FlightId, cancellationToken);

            await _flights.DeleteAsync(serial, flight.FlightId, cancellationToken);

            _logger.LogInformation(
                "Dropped flight {FlightId} of {Serial} and its {Rows} rows; only the newest " +
                "{Keep} flights are kept. The board may still hold this one.",
                flight.FlightId, serial, removed, _options.FlightsKeptPerAircraft);
        }
    }

    private static TelemetrySample ToSample(
        string serial, int flightId, TelemetrySampleDto dto, DateTime now) => new()
    {
        Serial = serial,
        FlightId = flightId,
        Index = dto.Index,
        TMs = dto.TMs,

        // Whatever the board sent is UTC; it has no other kind of time. Saying
        // so explicitly stops the driver storing it as unspecified and a reader
        // later shifting it by the server's offset.
        Utc = dto.Utc.HasValue
            ? DateTime.SpecifyKind(dto.Utc.Value.ToUniversalTime(), DateTimeKind.Utc)
            : null,

        // 0 or 1 on the wire, because it comes out of a CSV cell. Anything else
        // is not a boolean and is treated as "not reported" rather than guessed.
        Armed = dto.Armed switch
        {
            0 => false,
            1 => true,
            _ => null,
        },

        Mode = dto.Mode,
        GpsFix = dto.GpsFix,
        Sats = dto.Sats,
        Hdop = dto.Hdop,
        Lat = dto.Lat,
        Lon = dto.Lon,
        AltMsl = dto.AltMsl,
        AltRel = dto.AltRel,
        GroundSpeed = dto.GroundSpeed,
        AirSpeed = dto.AirSpeed,
        Climb = dto.Climb,
        Heading = dto.Heading,
        Cog = dto.Cog,
        Roll = dto.Roll,
        Pitch = dto.Pitch,
        Yaw = dto.Yaw,
        Throttle = dto.Throttle,
        BatteryVoltage = dto.BatteryVoltage,
        BatteryCurrent = dto.BatteryCurrent,
        BatteryRemaining = dto.BatteryRemaining,
        ConsumedMah = dto.ConsumedMah,
        RcRssi = dto.RcRssi,
        WifiRssi = dto.WifiRssi,
        LinkAgeMs = dto.LinkAgeMs,
        ReceivedAt = now,
    };
}
