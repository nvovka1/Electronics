namespace Initiator.Web.Contracts;

/// <summary>
/// A run of consecutive rows from one flight, as the board sends them.
/// </summary>
/// <remarks>
/// The flight is identified in the body rather than in the route. That is what
/// lets one request both open a flight and carry its first rows — the board has
/// no separate "start a flight" call to make, and therefore no half-created
/// flight to recover from if it makes that call and then loses power.
/// </remarks>
public sealed class TelemetryBatchRequest
{
    /// <summary>The board's flight number: its boot count, so one per power-on.</summary>
    public int FlightId { get; set; }

    /// <summary>
    /// The index of the first row in <see cref="Samples"/>. Informational — each
    /// row carries its own index, and that is what is stored. Kept because it
    /// makes a request legible in a log without reading the array.
    /// </summary>
    public int FirstIndex { get; set; }

    public List<TelemetrySampleDto> Samples { get; set; } = [];
}

/// <summary>
/// One row on the wire. Every measurement is nullable: the board sends null for
/// anything nobody reported, and storing that as zero would lose the difference
/// between a reading and a silence.
/// </summary>
public sealed class TelemetrySampleDto
{
    public int Index { get; set; }

    public long TMs { get; set; }

    public DateTime? Utc { get; set; }

    /// <summary>0 or 1 on the wire, because it comes straight out of a CSV cell.</summary>
    public int? Armed { get; set; }

    public int? Mode { get; set; }

    public int? GpsFix { get; set; }

    public int? Sats { get; set; }

    public double? Hdop { get; set; }

    public double? Lat { get; set; }

    public double? Lon { get; set; }

    public double? AltMsl { get; set; }

    public double? AltRel { get; set; }

    public double? GroundSpeed { get; set; }

    public double? AirSpeed { get; set; }

    public double? Climb { get; set; }

    public double? Heading { get; set; }

    public double? Cog { get; set; }

    public double? Roll { get; set; }

    public double? Pitch { get; set; }

    public double? Yaw { get; set; }

    public int? Throttle { get; set; }

    public double? BatteryVoltage { get; set; }

    public double? BatteryCurrent { get; set; }

    public int? BatteryRemaining { get; set; }

    public int? ConsumedMah { get; set; }

    public int? RcRssi { get; set; }

    public int? WifiRssi { get; set; }

    public long? LinkAgeMs { get; set; }
}
