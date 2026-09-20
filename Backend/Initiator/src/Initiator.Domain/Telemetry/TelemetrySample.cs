namespace Initiator.Domain.Telemetry;

/// <summary>
/// One row of a flight: what the aircraft was doing at one instant, as its
/// flight controller reported it.
/// </summary>
/// <remarks>
/// <para>
/// <b>Every measurement is nullable, and that is the whole point.</b> The board
/// writes an empty cell for anything nobody has reported, and it arrives here as
/// null. A battery reading 0.00 V and a battery nobody has mentioned are
/// completely different situations, and once both are stored as zero there is no
/// getting them apart again.
/// </para>
/// <para>
/// The columns match the CSV the board writes, one for one. That list lives in
/// <c>ESP32/mavlink/lib/telemetry/csv_row.h</c> as well as here, and a test
/// asserts the two agree — see <c>TelemetryCsvContractTests</c>.
/// </para>
/// </remarks>
public sealed class TelemetrySample
{
    public string Id { get; set; } = string.Empty;

    /// <summary>The aircraft's serial, as the board reports itself.</summary>
    public required string Serial { get; set; }

    /// <summary>The board's flight number: its boot count, so one per power-on.</summary>
    public int FlightId { get; set; }

    /// <summary>
    /// Position within the flight, starting at zero. Together with the serial and
    /// the flight it identifies the row, which is what makes a re-sent batch
    /// harmless.
    /// </summary>
    public int Index { get; set; }

    /// <summary>Milliseconds since the board booted. Always present: it is the board's own clock.</summary>
    public long TMs { get; set; }

    /// <summary>
    /// Wall clock, from the flight controller's GPS. Null until the GPS has a
    /// fix — the board itself has no idea what time it is.
    /// </summary>
    public DateTime? Utc { get; set; }

    public bool? Armed { get; set; }

    /// <summary>ArduPilot's custom_mode, raw. Its meaning depends on the frame type, so it is not decoded here.</summary>
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

    /// <summary>RC link signal strength as the flight controller reports it, 0-254.</summary>
    public int? RcRssi { get; set; }

    /// <summary>The board's own WiFi signal. Not from MAVLink — it is how far the aircraft is from the uploader.</summary>
    public int? WifiRssi { get; set; }

    /// <summary>
    /// How long before this row the last heartbeat arrived. The field that says
    /// whether to trust the rest of the row: a climbing value is a flight
    /// controller that has stopped talking while the board kept recording.
    /// </summary>
    public long? LinkAgeMs { get; set; }

    /// <summary>When this service received the row, which may be long after the flight.</summary>
    public DateTime ReceivedAt { get; set; }
}
