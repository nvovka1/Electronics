using LoraFleet.DataAccess.Repositories;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;

namespace LoraFleet.DataAccess;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddLoraFleetDataAccess(
        this IServiceCollection services, IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configuration);

        services.Configure<MongoOptions>(configuration.GetSection(MongoOptions.SectionName));

        // The driver's client is itself a connection pool, so one context for
        // the process. Creating one per request is how an Atlas connection
        // limit gets exhausted under trivial load.
        services.AddSingleton<FleetDbContext>();

        services.AddScoped<IDeviceRepository, MongoDeviceRepository>();
        services.AddScoped<ILogRepository, MongoLogRepository>();
        services.AddScoped<IFirmwareRepository, MongoFirmwareRepository>();

        services.AddHostedService<MongoIndexInitializer>();

        return services;
    }
}
