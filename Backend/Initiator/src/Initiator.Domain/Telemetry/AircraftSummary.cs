namespace Initiator.Domain.Telemetry;

/// <summary>
/// One aircraft, as an aggregate over its flights. Not stored: see
/// <see cref="Flight"/> for why there is no aircraft document.
/// </summary>
public sealed class AircraftSummary
{
    public required string Serial { get; set; }

    public int FlightCount { get; set; }

    public long SampleCount { get; set; }

    public DateTime LastSampleAt { get; set; }

    public int LastFlightId { get; set; }
}
