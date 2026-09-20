using Initiator.Domain.Telemetry;

namespace Initiator.Web.Models;

public sealed class FlightListViewModel
{
    /// <summary>The aircraft being filtered to, or null for all of them.</summary>
    public string? Serial { get; init; }

    public IReadOnlyList<AircraftSummary> Aircraft { get; init; } = [];

    public IReadOnlyList<Flight> Flights { get; init; } = [];
}

public sealed class FlightDetailsViewModel
{
    public required Flight Flight { get; init; }

    public IReadOnlyList<TelemetrySample> Samples { get; init; } = [];

    /// <summary>How many rows the page shows before the reader has to download the file.</summary>
    public int PreviewRows { get; init; }
}
