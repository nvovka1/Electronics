using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.Extensions.Options;

namespace Initiator.Web.Infrastructure;

/// <summary>
/// Requires a matching X-Api-Key header on device-facing endpoints.
/// </summary>
/// <remarks>
/// A shared key is the weakest thing still worth having: it stops a stranger who
/// finds the URL from enrolling devices and filling the log. It does not
/// authenticate an individual node — compromise one and you have the key for all
/// of them.
///
/// It is worth being plain about what that means here, because this service
/// dispatches commands rather than firmware versions: anyone holding the key can
/// queue a command for any enrolled node. The state machine on the node is what
/// stops that being worse than it sounds — a queued FIRE is refused unless the
/// node is already ARMED — but the key is not a substitute for keeping the
/// service off the open internet.
/// </remarks>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method)]
public sealed class ApiKeyAttribute : Attribute, IAsyncActionFilter
{
    public const string HeaderName = "X-Api-Key";

    public async Task OnActionExecutionAsync(
        ActionExecutingContext context, ActionExecutionDelegate next)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(next);

        var options = context.HttpContext.RequestServices
            .GetRequiredService<IOptions<InitiatorOptions>>().Value;

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
    /// Fixed-time comparison. A naive string compare leaks the key one byte at a
    /// time to anyone patient enough to measure the response.
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
