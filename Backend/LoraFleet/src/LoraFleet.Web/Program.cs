using LoraFleet.DataAccess;
using LoraFleet.Web.Infrastructure;
using LoraFleet.Web.Services;
using Microsoft.Extensions.Options;

var builder = WebApplication.CreateBuilder(args);

// Configuration comes from the environment in a container: MONGO__CONNECTIONSTRING
// maps onto Mongo:ConnectionString, FLEET__DEVICEAPIKEY onto Fleet:DeviceApiKey.
// The connection string is never in appsettings.json - it carries a password.
builder.Configuration.AddEnvironmentVariables();

builder.Services.Configure<FleetOptions>(builder.Configuration.GetSection(FleetOptions.SectionName));
builder.Services.AddSingleton(TimeProvider.System);

builder.Services.AddLoraFleetDataAccess(builder.Configuration);

builder.Services.AddScoped<IDeviceCheckInService, DeviceCheckInService>();
builder.Services.AddScoped<IFirmwareService, FirmwareService>();

// Only does anything when SeedDemoData is true and no devices are enrolled, so
// it can never overwrite a real fleet.
builder.Services.AddHostedService<DemoDataSeeder>();

builder.Services.AddControllersWithViews();

// The device-facing API speaks camelCase, like every other JSON on the wire in
// this system. Configured once here rather than annotated per property.
builder.Services.ConfigureHttpJsonOptions(options =>
{
    options.SerializerOptions.PropertyNamingPolicy = System.Text.Json.JsonNamingPolicy.CamelCase;
});

builder.Services.AddHealthChecks();

var app = builder.Build();

// An open ingest endpoint lets anyone enrol devices and fill the log, so an
// unset key is a configuration error everywhere except a developer's machine.
// Failing at start-up beats discovering it from an access log.
var fleetOptions = app.Services.GetRequiredService<IOptions<FleetOptions>>().Value;

if (!app.Environment.IsDevelopment() && string.IsNullOrEmpty(fleetOptions.DeviceApiKey))
{
    throw new InvalidOperationException(
        "Fleet:DeviceApiKey is not set. Put FLEET__DEVICEAPIKEY in the environment " +
        "(see .env.example) before running outside Development.");
}

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Home/Error");
    app.UseHsts();
}

// Deliberately no UseHttpsRedirection: the container serves plain HTTP and TLS
// is terminated by whatever sits in front of it. Redirecting here would send a
// device to a port the container is not listening on.

app.UseStaticFiles();
app.UseRouting();

app.MapControllers();
app.MapDefaultControllerRoute();
app.MapHealthChecks("/healthz");

app.Run();

/// <summary>Exposed so the tests can build the same host.</summary>
public partial class Program;
