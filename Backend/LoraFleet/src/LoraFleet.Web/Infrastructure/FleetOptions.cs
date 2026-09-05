namespace LoraFleet.Web.Infrastructure;

public sealed class FleetOptions
{
    public const string SectionName = "Fleet";

    /// <summary>
    /// Shared secret devices send in the X-Api-Key header. Empty disables the
    /// check, which is refused outside Development: an open ingest endpoint
    /// lets anyone enrol a device and write its log.
    /// </summary>
    public string DeviceApiKey { get; set; } = string.Empty;

    /// <summary>
    /// How often a node is expected to check in. Defaults to the firmware's own
    /// health_period_s default. Only used to decide when silence is worth
    /// noticing - it is not pushed to the devices.
    /// </summary>
    public int ExpectedReportPeriodSeconds { get; set; } = 60;

    /// <summary>
    /// Public base URL, used to build absolute download links in the update
    /// manifest. Behind a reverse proxy the request's own host is often the
    /// container's, not the one a device can reach.
    /// </summary>
    public string? PublicBaseUrl { get; set; }

    public TimeSpan ExpectedReportPeriod =>
        TimeSpan.FromSeconds(Math.Max(1, ExpectedReportPeriodSeconds));
}
