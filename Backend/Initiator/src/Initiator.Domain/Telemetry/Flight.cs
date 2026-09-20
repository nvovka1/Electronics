namespace Initiator.Domain.Telemetry;

/// <summary>
/// One power-on of one aircraft's logger, and a summary of what arrived.
/// </summary>
/// <remarks>
/// There is no separate Aircraft collection. An aircraft is exactly the set of
/// flights carrying its serial, and a second document saying so would be a
/// second thing to keep in step for no question it answers that an aggregate
/// over this collection cannot.
/// </remarks>
public sealed class Flight
{
    /// <summary>
    /// <c>serial:flightId</c>. A natural key, like the device serial elsewhere in
    /// this service: a generated ObjectId would add a second identity for the
    /// same thing and an upsert would need a lookup first.
    /// </summary>
    public string Id { get; set; } = string.Empty;

    public required string Serial { get; set; }

    /// <summary>The board's flight number — its boot count, so one per power-on.</summary>
    public int FlightId { get; set; }

    /// <summary>When this service first heard about the flight, on its own clock.</summary>
    public DateTime FirstSeenAt { get; set; }

    /// <summary>When the most recent batch arrived, on this service's clock.</summary>
    public DateTime LastSampleAt { get; set; }

    /// <summary>
    /// The earliest wall-clock stamp in the flight, or null when it flew without
    /// a GPS fix. Not the same as <see cref="FirstSeenAt"/>: a flight recorded
    /// out of WiFi range can arrive hours after it happened, and this is the one
    /// worth sorting by.
    /// </summary>
    public DateTime? StartedAt { get; set; }

    public DateTime? EndedAt { get; set; }

    public int SampleCount { get; set; }

    /// <summary>
    /// The index this service expects next: the highest it holds, plus one.
    /// Handed back on every ingest so the board can set its cursor from what the
    /// service actually has rather than from what it believes it sent.
    /// </summary>
    public int NextIndex { get; set; }

    public double? MaxAltRel { get; set; }

    public double? MaxGroundSpeed { get; set; }

    public double? MinBatteryVoltage { get; set; }

    /// <summary>Flight duration by the board's own clock, which is present even when the GPS never was.</summary>
    public long DurationMs { get; set; }
}
