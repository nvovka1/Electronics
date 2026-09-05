using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.Extensions.Options;

namespace LoraFleet.Web.Infrastructure;

/// <summary>
/// Requires a matching X-Api-Key header on device-facing endpoints.
/// </summary>
/// <remarks>
/// A shared key is the weakest thing that is still worth having: it keeps a
/// stranger who finds the URL from enrolling devices and filling the log. It
/// does not authenticate an individual node - compromise one device and you
/// have the key for all of them - which is why the key never protects anything
/// destructive. Per-device credentials are the next step if this fleet ever
/// carries something worth stealing.
/// </remarks>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method)]
public sealed class ApiKeyAttribute : Attribute, IAsyncActionFilter
{
    public const string HeaderName = "X-Api-Key";

    public async Task OnActionExecutionAsync(ActionExecutingContext context, ActionExecutionDelegate next)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(next);

        var options = context.HttpContext.RequestServices
            .GetRequiredService<IOptions<FleetOptions>>().Value;

        // An unset key means the check is off. Program.cs refuses to start that
        // way outside Development, so this branch only ever runs on a bench.
        if (string.IsNullOrEmpty(options.DeviceApiKey))
        {
            await next();
            return;
        }

        var provided = context.HttpContext.Request.Headers[HeaderName].ToString();

        if (!IsMatch(provided, options.DeviceApiKey))
        {
            context.Result = new UnauthorizedObjectResult(new ProblemDetails
            {
                Title = "Missing or invalid API key",
                Detail = $"Send the fleet key in the {HeaderName} header.",
                Status = StatusCodes.Status401Unauthorized,
            });
            return;
        }

        await next();
    }

    /// <summary>
    /// Fixed-time comparison. A naive string compare leaks the key one byte at
    /// a time to anyone patient enough to measure the response.
    /// </summary>
    private static bool IsMatch(string provided, string expected)
    {
        var providedBytes = Encoding.UTF8.GetBytes(provided);
        var expectedBytes = Encoding.UTF8.GetBytes(expected);

        // FixedTimeEquals still returns early on a length mismatch, so the
        // length of the key is not a secret. Its contents are.
        return providedBytes.Length == expectedBytes.Length &&
               CryptographicOperations.FixedTimeEquals(providedBytes, expectedBytes);
    }
}
