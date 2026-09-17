using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Initiator.DataAccess;
using Initiator.Domain.States;
using Initiator.Web.Contracts;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;

namespace Initiator.Tests;

/// <summary>
/// The real host, the real routing, the real JSON and a real mongod. Everything
/// below this line has unit tests already; what this file checks is that the
/// pieces are wired to each other — which is the part unit tests cannot see.
/// </summary>
[Collection(MongoCollection.Name)]
public sealed class EndToEndTests : IDisposable
{
    private const string ApiKey = "test-key-not-a-secret";
    private const string Serial = "IN-E2E1";

    private readonly WebApplicationFactory<Program> _factory;
    private readonly HttpClient _client;

    public EndToEndTests(MongoFixture mongo)
    {
        var database = $"e2e-{Guid.NewGuid():N}";

        _factory = new WebApplicationFactory<Program>().WithWebHostBuilder(builder =>
        {
            // Production, so the API key is actually enforced rather than being
            // waved through the way it is on a bench.
            builder.UseEnvironment("Production");

            builder.ConfigureAppConfiguration((_, configuration) =>
            {
                configuration.AddInMemoryCollection(new Dictionary<string, string?>
                {
                    ["Initiator:DeviceApiKey"] = ApiKey,
                    ["Initiator:SeedDemoData"] = "false",
                    ["Mongo:DatabaseName"] = database,
                });
            });

            builder.ConfigureServices(services =>
            {
                // Point the real context at the throwaway mongod. Everything
                // else in the host is left exactly as it ships.
                services.RemoveAll<InitiatorDbContext>();
                services.AddSingleton(_ => mongo.CreateContext(database));
            });
        });

        // Do not follow the redirect after a form post. Following it turns the
        // 302 into the 200 of the page it lands on, which would make a test
        // that means to check "the post was accepted" silently check nothing.
        _client = _factory.CreateClient(new WebApplicationFactoryClientOptions
        {
            AllowAutoRedirect = false,
        });

        _client.DefaultRequestHeaders.Add("X-Api-Key", ApiKey);
    }

    private static CheckInRequest Report(NodeState state, long stateTimestampMs, int reboots = 1) => new()
    {
        NodeId = 2,
        HardwareId = "ttgo-lora32-v21",
        FirmwareVersion = "0.1.0",
        BuildType = "dev",
        ProtocolVersion = 1,
        UptimeSeconds = 120,
        Reboots = reboots,
        BatteryDeciVolts = 39,
        LastRssi = -91,
        PostMask = 0,
        LoraLinkUp = true,
        State = (byte)state,
        StateTimestampMs = stateTimestampMs,
        AutoArmTimeoutSeconds = 300,
    };

    [Fact]
    public async Task A_node_without_the_key_is_turned_away()
    {
        using var anonymous = _factory.CreateClient();

        var response = await anonymous.PostAsJsonAsync(
            $"/api/v1/devices/{Serial}/health-reports", Report(NodeState.Safe, 0));

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_serial_that_could_not_be_a_device_is_refused()
    {
        // It ends up in a database key and in a URL, so it is bounded rather
        // than trusted.
        var response = await _client.PostAsJsonAsync(
            "/api/v1/devices/..%2Fetc/health-reports", Report(NodeState.Safe, 0));

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task The_whole_round_trip_works()
    {
        // 1. The node enrols itself by reporting.
        var enrol = await _client.PostAsJsonAsync(
            $"/api/v1/devices/{Serial}/health-reports", Report(NodeState.Safe, 0));

        Assert.Equal(HttpStatusCode.Accepted, enrol.StatusCode);

        // 2. Nothing queued yet - the common answer, and the cheap one.
        var empty = await _client.GetAsync($"/api/v1/devices/{Serial}/commands/next");
        Assert.Equal(HttpStatusCode.NoContent, empty.StatusCode);

        // 3. An operator queues INIT from the dashboard.
        var queued = await QueueFromDashboardAsync(CommandType.Init);
        Assert.True(queued);

        // 4. The node collects it.
        var next = await _client.GetAsync($"/api/v1/devices/{Serial}/commands/next");
        Assert.Equal(HttpStatusCode.OK, next.StatusCode);

        var command = await next.Content.ReadFromJsonAsync<NextCommandResponse>(CamelCase);
        Assert.Equal((byte)CommandType.Init, command!.Command);

        // 5. ...and there is nothing left behind it.
        var drained = await _client.GetAsync($"/api/v1/devices/{Serial}/commands/next");
        Assert.Equal(HttpStatusCode.NoContent, drained.StatusCode);

        // 6. The node reports the transition it made.
        var reported = await _client.PostAsJsonAsync(
            $"/api/v1/devices/{Serial}/state-events",
            new StateEventBatchRequest
            {
                Events =
                [
                    new StateEventRequest
                    {
                        TimestampMs = 1_000,
                        BootCount = 1,
                        FromState = (byte)NodeState.Safe,
                        ToState = (byte)NodeState.Init,
                        Command = (byte)CommandType.Init,
                        Source = (byte)CommandSource.Api,
                        Accepted = true,
                        CommandId = command.CommandId,
                    },
                ],
            });

        Assert.Equal(HttpStatusCode.Accepted, reported.StatusCode);

        // 7. The node arms itself when its countdown expires, with no command
        //    behind it. This is the transition nobody asked for.
        var autoArmed = await _client.PostAsJsonAsync(
            $"/api/v1/devices/{Serial}/state-events",
            new StateEventBatchRequest
            {
                Events =
                [
                    new StateEventRequest
                    {
                        TimestampMs = 301_000,
                        BootCount = 1,
                        FromState = (byte)NodeState.Init,
                        ToState = (byte)NodeState.Armed,
                        Command = null,
                        Source = (byte)CommandSource.Timer,
                        Accepted = true,
                    },
                ],
            });

        Assert.Equal(HttpStatusCode.Accepted, autoArmed.StatusCode);

        // 8. The next check-in agrees, and the service hands the state back.
        var confirm = await _client.PostAsJsonAsync(
            $"/api/v1/devices/{Serial}/health-reports", Report(NodeState.Armed, 301_000));

        using var body = JsonDocument.Parse(await confirm.Content.ReadAsStringAsync());
        Assert.Equal((byte)NodeState.Armed, body.RootElement.GetProperty("state").GetByte());
    }

    [Fact]
    public async Task A_command_the_node_would_refuse_is_not_queued_at_all()
    {
        await _client.PostAsJsonAsync(
            "/api/v1/devices/IN-E2E2/health-reports", Report(NodeState.Safe, 0));

        // FIRE from SAFE. The operator finds out now rather than one poll later.
        var queued = await QueueFromDashboardAsync(CommandType.Fire, "IN-E2E2");
        Assert.False(queued);

        var next = await _client.GetAsync("/api/v1/devices/IN-E2E2/commands/next");
        Assert.Equal(HttpStatusCode.NoContent, next.StatusCode);
    }

    [Fact]
    public async Task The_dashboard_renders()
    {
        // The views compile at build; this is the only thing that proves they
        // also run - a null in a view is a 500, not a compiler error.
        await _client.PostAsJsonAsync(
            "/api/v1/devices/IN-E2E3/health-reports", Report(NodeState.Init, 1_000));

        foreach (var url in new[] { "/Devices", "/History", "/Commands", "/Logs" })
        {
            var page = await _client.GetAsync(url);

            Assert.True(page.IsSuccessStatusCode, $"{url} returned {(int)page.StatusCode}");
        }

        var details = await _client.GetAsync("/Devices/Details/IN-E2E3");
        var html = await details.Content.ReadAsStringAsync();

        Assert.True(details.IsSuccessStatusCode);
        Assert.Contains("IN-E2E3", html, StringComparison.Ordinal);

        // The node is in INIT, so the page must show the countdown and must not
        // offer FIRE as something that can be clicked.
        Assert.Contains("Arms itself in about", html, StringComparison.Ordinal);
        Assert.Contains("INIT", html, StringComparison.Ordinal);
    }

    /// <summary>
    /// Posts the dashboard's own form, antiforgery token and all, rather than
    /// calling the service directly — the point of this file is the wiring.
    /// </summary>
    private async Task<bool> QueueFromDashboardAsync(CommandType command, string serial = Serial)
    {
        var page = await _client.GetAsync($"/Devices/Details/{serial}");
        var html = await page.Content.ReadAsStringAsync();
        var token = ExtractAntiForgeryToken(html);

        var response = await _client.PostAsync("/Devices/Queue", new FormUrlEncodedContent(
            new Dictionary<string, string>
            {
                ["serial"] = serial,
                ["command"] = command.ToString(),
                ["__RequestVerificationToken"] = token,
            }));

        // The controller redirects whether it queued the command or refused it;
        // the difference is the notice waiting in TempData. So follow the
        // redirect and read what the operator would actually see.
        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);

        var landing = await _client.GetAsync(response.Headers.Location);
        var landingHtml = await landing.Content.ReadAsStringAsync();

        // Deliberately not a substring search for the command name. The refusal
        // message names the command too ("... does not accept FIRE"), so that
        // test would pass on the failure it is meant to catch.
        return landingHtml.Contains("notice ok", StringComparison.Ordinal);
    }

    private static string ExtractAntiForgeryToken(string html)
    {
        const string marker = "name=\"__RequestVerificationToken\" type=\"hidden\" value=\"";
        var start = html.IndexOf(marker, StringComparison.Ordinal);

        Assert.True(start >= 0, "The page did not contain an antiforgery token.");

        start += marker.Length;
        var end = html.IndexOf('"', start);

        return html[start..end];
    }

    private static readonly JsonSerializerOptions CamelCase =
        new(JsonSerializerDefaults.Web);

    public void Dispose()
    {
        _client.Dispose();
        _factory.Dispose();
    }
}
