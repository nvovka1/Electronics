namespace Initiator.Web.Infrastructure;

public sealed class InitiatorOptions
{
    public const string SectionName = "Initiator";

    /// <summary>
    /// Shared secret devices send in the X-Api-Key header. Empty disables the
    /// check, which is refused outside Development: an open ingest endpoint lets
    /// anyone enrol a device, queue commands for it and fill its log.
    /// </summary>
    public string DeviceApiKey { get; set; } = string.Empty;

    /// <summary>
    /// How often a node is expected to check in. Only used to decide when
    /// silence is worth noticing — it is not pushed to the devices.
    /// </summary>
    public int ExpectedReportPeriodSeconds { get; set; } = 30;

    public TimeSpan ExpectedReportPeriod =>
        TimeSpan.FromSeconds(Math.Max(1, ExpectedReportPeriodSeconds));

    /// <summary>
    /// Fills an empty database with a few plausible nodes so the dashboard can
    /// be seen working before any hardware exists. Only runs when the Devices
    /// collection is empty, so it can never overwrite real check-ins.
    /// </summary>
    public bool SeedDemoData { get; set; }
}
