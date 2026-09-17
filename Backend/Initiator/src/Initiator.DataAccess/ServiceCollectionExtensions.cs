using Initiator.DataAccess.Repositories;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;

namespace Initiator.DataAccess;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddInitiatorDataAccess(
        this IServiceCollection services, IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configuration);

        services.Configure<MongoOptions>(configuration.GetSection(MongoOptions.SectionName));

        // The driver's client is itself a connection pool, so one context for
        // the process. Creating one per request is how a connection limit gets
        // exhausted under trivial load.
        services.AddSingleton<InitiatorDbContext>();

        services.AddScoped<IDeviceRepository, MongoDeviceRepository>();
        services.AddScoped<ICommandRepository, MongoCommandRepository>();
        services.AddScoped<IStateEventRepository, MongoStateEventRepository>();
        services.AddScoped<ILogRepository, MongoLogRepository>();

        services.AddHostedService<MongoIndexInitializer>();

        return services;
    }
}
