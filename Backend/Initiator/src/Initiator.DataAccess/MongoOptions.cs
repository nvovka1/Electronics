namespace Initiator.DataAccess;

/// <summary>
/// Where this service's data lives. The connection string is never committed:
/// it comes from the environment (see .env.example) or from user-secrets
/// locally.
/// </summary>
public sealed class MongoOptions
{
    public const string SectionName = "Mongo";

    /// <summary>Full mongodb:// or mongodb+srv:// URI, including credentials.</summary>
    public string ConnectionString { get; set; } = string.Empty;

    /// <summary>
    /// Its own database, not a collection inside LoraFleet's. A fleet-management
    /// problem must not be able to take down command dispatch.
    /// </summary>
    public string DatabaseName { get; set; } = "initiator";

    /// <summary>
    /// How long node log records are kept. Logs are the bulk of the data and the
    /// least valuable part of it once a problem is closed.
    /// </summary>
    public int LogRetentionDays { get; set; } = 30;

    /// <summary>
    /// How long state events are kept. Longer than logs on purpose: this is the
    /// record of what the nodes actually did, which is the thing anyone will
    /// want to look at afterwards.
    /// </summary>
    public int StateEventRetentionDays { get; set; } = 365;
}
