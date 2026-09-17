using EphemeralMongo;
using Initiator.DataAccess;
using Microsoft.Extensions.Options;

namespace Initiator.Tests;

/// <summary>
/// A real mongod, started for the test run and thrown away afterwards.
/// </summary>
/// <remarks>
/// The repositories are thin, but the parts that are easy to get wrong cannot be
/// checked against a mock, because a mock agrees with whatever the code happens
/// to do. Those parts are the BSON mapping (a natural string key, computed
/// properties that must not be persisted), the upsert that has to leave the
/// operator's fields alone, the conditional state write, and the atomic claim.
/// </remarks>
public sealed class MongoFixture : IDisposable
{
    private readonly IMongoRunner _runner;

    public MongoFixture()
    {
        // A plain standalone node: nothing here needs transactions, and a
        // single-node replica set costs several seconds of election on start.
        _runner = MongoRunner.Run(new MongoRunnerOptions
        {
            UseSingleNodeReplicaSet = false,
        });
    }

    public InitiatorDbContext CreateContext(string databaseName)
    {
        return new InitiatorDbContext(Options.Create(new MongoOptions
        {
            ConnectionString = _runner.ConnectionString,
            DatabaseName = databaseName,
        }));
    }

    public void Dispose() => _runner.Dispose();
}

[CollectionDefinition(Name)]
public sealed class MongoCollection : ICollectionFixture<MongoFixture>
{
    public const string Name = "mongo";
}
