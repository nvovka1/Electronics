namespace LoraFleet.DataAccess;

/// <summary>
/// Where the fleet's data lives. The connection string is never committed: it
/// comes from the environment (see .env.example) or from user-secrets locally.
/// </summary>
public sealed class MongoOptions
{
    public const string SectionName = "Mongo";

    /// <summary>Full mongodb:// or mongodb+srv:// URI, including credentials.</summary>
    public string ConnectionString { get; set; } = string.Empty;

    public string DatabaseName { get; set; } = "lorafleet";

    /// <summary>
    /// How long a node's log records are kept. Logs are the bulk of the data
    /// and the least valuable part of it once a problem is closed, so they
    /// expire rather than growing without limit.
    /// </summary>
    public int LogRetentionDays { get; set; } = 30;
}
