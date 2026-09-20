using System.Globalization;
using System.Text;
using Initiator.Domain.Telemetry;

namespace Initiator.Web.Infrastructure;

/// <summary>
/// The CSV a flight is exported as. The same columns, in the same order and the
/// same formats, that the board writes to its own flash.
/// </summary>
/// <remarks>
/// <para>
/// <b><see cref="Header"/> is a contract</b> with
/// <c>ESP32/mavlink/lib/telemetry/csv_row.h</c>. The list exists twice because
/// one copy runs on a server and the other on a board with no .NET; what there
/// can be is a build that fails when they drift, which is
/// <c>TelemetryCsvContractTests</c>.
/// </para>
/// <para>
/// Without it the two would diverge silently, and the symptom would be a
/// downloaded spreadsheet whose columns are all shifted by one — numbers that
/// look plausible, are wrong, and get believed.
/// </para>
/// </remarks>
public static class TelemetryCsv
{
    // CSV-HEADER-BEGIN
    public const string Header =
        "index,tMs,utc,armed,mode,gpsFix,sats,hdop,lat,lon,altMsl,altRel,groundSpeed," +
        "airSpeed,climb,heading,cog,roll,pitch,yaw,throttle,batteryVoltage,batteryCurrent," +
        "batteryRemaining,consumedMah,rcRssi,wifiRssi,linkAgeMs";
    // CSV-HEADER-END

    /// <summary>
    /// One row. A null is written as an empty cell, exactly as the board writes
    /// it — the difference between a battery reading zero and a battery nobody
    /// has reported survives the round trip.
    /// </summary>
    public static string Row(TelemetrySample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);

        var builder = new StringBuilder(220);

        builder.Append(sample.Index.ToString(CultureInfo.InvariantCulture)).Append(',');
        builder.Append(sample.TMs.ToString(CultureInfo.InvariantCulture)).Append(',');

        // The board writes milliseconds, so this does too. Round-tripping a
        // downloaded file through the board's own parser is a thing people do.
        builder.Append(sample.Utc?.ToString("yyyy-MM-ddTHH:mm:ss.fffZ", CultureInfo.InvariantCulture))
               .Append(',');

        builder.Append(sample.Armed is null ? string.Empty : sample.Armed.Value ? "1" : "0").Append(',');
        Append(builder, sample.Mode);
        Append(builder, sample.GpsFix);
        Append(builder, sample.Sats);
        Append(builder, sample.Hdop, 2);
        Append(builder, sample.Lat, 7);
        Append(builder, sample.Lon, 7);
        Append(builder, sample.AltMsl, 2);
        Append(builder, sample.AltRel, 2);
        Append(builder, sample.GroundSpeed, 2);
        Append(builder, sample.AirSpeed, 2);
        Append(builder, sample.Climb, 2);
        Append(builder, sample.Heading, 1);
        Append(builder, sample.Cog, 1);
        Append(builder, sample.Roll, 1);
        Append(builder, sample.Pitch, 1);
        Append(builder, sample.Yaw, 1);
        Append(builder, sample.Throttle);
        Append(builder, sample.BatteryVoltage, 3);
        Append(builder, sample.BatteryCurrent, 2);
        Append(builder, sample.BatteryRemaining);
        Append(builder, sample.ConsumedMah);
        Append(builder, sample.RcRssi);
        Append(builder, sample.WifiRssi);

        // No trailing separator on the last column.
        if (sample.LinkAgeMs.HasValue)
        {
            builder.Append(sample.LinkAgeMs.Value.ToString(CultureInfo.InvariantCulture));
        }

        return builder.ToString();
    }

    private static void Append(StringBuilder builder, int? value)
    {
        if (value.HasValue) builder.Append(value.Value.ToString(CultureInfo.InvariantCulture));
        builder.Append(',');
    }

    private static void Append(StringBuilder builder, double? value, int decimals)
    {
        if (value.HasValue)
        {
            builder.Append(value.Value.ToString("F" + decimals, CultureInfo.InvariantCulture));
        }
        builder.Append(',');
    }
}
