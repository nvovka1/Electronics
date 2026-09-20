using System.Globalization;
using System.Text.RegularExpressions;
using Initiator.Domain.Telemetry;
using Initiator.Web.Infrastructure;

namespace Initiator.Tests;

/// <summary>
/// Reads the CSV header out of the telemetry logger's firmware and asserts that
/// <see cref="TelemetryCsv"/> agrees with it.
/// </summary>
/// <remarks>
/// <para>
/// The column list exists twice — C++ there, C# here — and there is no way to
/// have only one copy: one runs on a server and the other on a board with no
/// .NET. What there can be is a build that fails when they drift, which is this.
/// </para>
/// <para>
/// Without it the symptom would be a downloaded spreadsheet whose columns are
/// all shifted by one. Every number in it looks plausible, every one of them is
/// attributed to the wrong thing, and nobody notices until a decision has been
/// made on it.
/// </para>
/// <para>
/// This and <see cref="FirmwareTransitionTableContractTests"/> are the only
/// cross-project file access in the build, and both are read-only.
/// </para>
/// </remarks>
public sealed class TelemetryCsvContractTests
{
    private const string FirmwareRelativePath = "ESP32/mavlink/lib/telemetry/csv_row.h";

    [Fact]
    public void The_service_and_the_firmware_agree_on_the_columns()
    {
        Assert.Equal(TelemetryCsv.Header, ReadFirmwareHeader());
    }

    [Fact]
    public void An_unreported_value_is_an_empty_cell_and_not_a_zero()
    {
        // The distinction the whole schema is built around: a battery reading
        // 0.00 V and a battery nobody has mentioned are different situations.
        var row = TelemetryCsv.Row(new TelemetrySample { Serial = "uav-1", Index = 3, TMs = 1500 });

        var cells = row.Split(',');

        Assert.Equal(TelemetryCsv.Header.Split(',').Length, cells.Length);
        Assert.Equal("3", cells[0]);
        Assert.Equal("1500", cells[1]);
        Assert.All(cells[2..], cell => Assert.Equal(string.Empty, cell));
    }

    [Fact]
    public void Numbers_are_written_the_way_the_board_writes_them()
    {
        var row = TelemetryCsv.Row(new TelemetrySample
        {
            Serial = "uav-1",
            Index = 0,
            TMs = 500,
            Utc = new DateTime(2026, 9, 20, 12, 0, 0, 500, DateTimeKind.Utc),
            Armed = true,
            Lat = 50.1234567,
            Lon = -1.2345678,
            AltRel = 35.25,
            BatteryVoltage = 16.8,
            LinkAgeMs = 250,
        });

        var cells = row.Split(',');
        var columns = TelemetryCsv.Header.Split(',');

        Assert.Equal("2026-09-20T12:00:00.500Z", cells[Array.IndexOf(columns, "utc")]);
        Assert.Equal("1", cells[Array.IndexOf(columns, "armed")]);
        Assert.Equal("50.1234567", cells[Array.IndexOf(columns, "lat")]);
        Assert.Equal("-1.2345678", cells[Array.IndexOf(columns, "lon")]);
        Assert.Equal("35.25", cells[Array.IndexOf(columns, "altRel")]);
        Assert.Equal("16.800", cells[Array.IndexOf(columns, "batteryVoltage")]);
        Assert.Equal("250", cells[Array.IndexOf(columns, "linkAgeMs")]);
    }

    [Fact]
    public void Decimals_do_not_follow_the_server_culture()
    {
        // A server set to a culture that writes 50,12 would produce a file with
        // an extra column per row, and the damage would look like corrupt data
        // rather than a formatting setting.
        var original = Thread.CurrentThread.CurrentCulture;
        try
        {
            Thread.CurrentThread.CurrentCulture = new CultureInfo("de-DE");

            var row = TelemetryCsv.Row(new TelemetrySample
            {
                Serial = "uav-1",
                Lat = 50.5,
                BatteryVoltage = 16.8,
            });

            Assert.Contains("50.5000000", row, StringComparison.Ordinal);
            Assert.Equal(TelemetryCsv.Header.Split(',').Length, row.Split(',').Length);
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = original;
        }
    }

    /// <summary>
    /// Pulls the pieces of the firmware's CSV_HEADER macro out and joins them.
    /// The macro is a line-continued run of string literals, which is how a
    /// 28-column header stays inside a sane line length in C.
    /// </summary>
    private static string ReadFirmwareHeader()
    {
        var source = ReadFirmwareSource();

        var start = source.IndexOf("#define CSV_HEADER", StringComparison.Ordinal);
        Assert.True(start >= 0, $"CSV_HEADER is not defined in {FirmwareRelativePath}.");

        // The macro ends at the first line that does not continue.
        var end = start;
        while (true)
        {
            var lineEnd = source.IndexOf('\n', end);
            if (lineEnd < 0)
            {
                end = source.Length;
                break;
            }

            var line = source[end..lineEnd].TrimEnd();
            end = lineEnd + 1;
            if (!line.EndsWith('\\')) break;
        }

        var pieces = Regex.Matches(source[start..end], "\"([^\"]*)\"")
                          .Select(match => match.Groups[1].Value);

        var header = string.Concat(pieces);

        Assert.False(
            string.IsNullOrWhiteSpace(header),
            $"CSV_HEADER was found in {FirmwareRelativePath} but no string literal could be read from it.");

        return header;
    }

    private static string ReadFirmwareSource()
    {
        var path = Path.Combine(
            FindRepositoryRoot(),
            FirmwareRelativePath.Replace('/', Path.DirectorySeparatorChar));

        Assert.True(
            File.Exists(path),
            $"The firmware source is not where this test expects it: {path}. " +
            $"It should be at {FirmwareRelativePath} relative to the repository root.");

        return File.ReadAllText(path);
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory.FullName, "ESP32")) &&
                Directory.Exists(Path.Combine(directory.FullName, "Backend")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find the repository root (a directory containing both ESP32 and Backend) " +
            $"walking up from {AppContext.BaseDirectory}.");
    }
}
