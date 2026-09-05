using EphemeralMongo;
using LoraFleet.DataAccess;
using Microsoft.Extensions.Options;

namespace LoraFleet.Tests;

/// <summary>
/// A real mongod, started for the test run and thrown away afterwards.
/// </summary>
/// <remarks>
/// The repositories are thin, but the part that is easy to get wrong is the
/// BSON mapping - a natural string key, computed properties that must not be
/// persisted, and an upsert that has to leave the operator's fields alone. None
/// of that can be checked against a mock, because a mock would agree with
/// whatever the mapping happened to do.
/// </remarks>
public sealed class MongoFixture : IDisposable
{
    private readonly IMongoRunner _runner;

    public MongoFixture()
    {
        _runner = MongoRunner.Run(new MongoRunnerOptions
        {
            UseSingleNodeReplicaSet = false,
            KillMongoProcessesWhenCurrentProcessExits = true,
        });
    }

    public FleetDbContext CreateContext(string databaseName)
    {
        return new FleetDbContext(Options.Create(new MongoOptions
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
