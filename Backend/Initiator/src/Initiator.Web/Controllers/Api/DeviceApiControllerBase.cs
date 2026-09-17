using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>
/// Shared by the device-facing endpoints. Exists for one reason: the serial
/// arrives in the URL of every one of them and has to be bounded before it
/// reaches a database key.
/// </summary>
public abstract class DeviceApiControllerBase : ControllerBase
{
    /// <summary>
    /// The serial ends up in a database key and in a URL, so it is checked here
    /// rather than trusted. Nodes derive it from the chip MAC when they have
    /// never been provisioned, so the format is not guaranteed to be pretty —
    /// only sane.
    /// </summary>
    protected static bool IsPlausibleSerial(string serial) =>
        !string.IsNullOrWhiteSpace(serial) &&
        serial.Length <= 32 &&
        serial.All(character => char.IsAsciiLetterOrDigit(character) || character is '-' or '_');

    protected IActionResult InvalidSerial() =>
        BadRequest(new ProblemDetails
        {
            Title = "Invalid serial",
            Detail = "A serial is up to 32 characters of letters, digits, hyphen or underscore.",
            Status = StatusCodes.Status400BadRequest,
        });
}
