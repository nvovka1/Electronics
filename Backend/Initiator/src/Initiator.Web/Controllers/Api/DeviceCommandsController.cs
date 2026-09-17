using Initiator.Web.Contracts;
using Initiator.Web.Infrastructure;
using Initiator.Web.Services;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers.Api;

/// <summary>
/// The polling path: how a node finds out about a command queued on the
/// dashboard, and how it reports what it did with it.
/// </summary>
[ApiController]
[ApiKey]
[Route("api/v1/devices/{serial}/commands")]
[Produces("application/json")]
public sealed class DeviceCommandsController : DeviceApiControllerBase
{
    private readonly ICommandDispatchService _dispatch;

    public DeviceCommandsController(ICommandDispatchService dispatch)
    {
        _dispatch = dispatch ?? throw new ArgumentNullException(nameof(dispatch));
    }

    /// <summary>
    /// The next command for this node, or 204 when there is nothing to do.
    /// </summary>
    /// <remarks>
    /// 204 is the common answer and the cheapest one for a device on a battery
    /// to handle: no body to parse and nothing to allocate. Claiming is atomic,
    /// so two polls in flight at once cannot both be handed the same command.
    /// </remarks>
    [HttpGet("next")]
    [ProducesResponseType(typeof(NextCommandResponse), StatusCodes.Status200OK)]
    [ProducesResponseType(StatusCodes.Status204NoContent)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Next(string serial, CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        var command = await _dispatch.ClaimNextAsync(serial, cancellationToken);

        if (command is null) return NoContent();

        return Ok(new NextCommandResponse
        {
            CommandId = command.Id,
            Command = (byte)command.Type,
            QueuedAt = command.QueuedAt,
        });
    }

    /// <summary>What the node did with a command it collected.</summary>
    [HttpPost("{commandId}/result")]
    [ProducesResponseType(StatusCodes.Status202Accepted)]
    [ProducesResponseType(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    public async Task<IActionResult> Result(
        string serial,
        string commandId,
        [FromBody] CommandResultRequest request,
        CancellationToken cancellationToken)
    {
        if (!IsPlausibleSerial(serial)) return InvalidSerial();

        await _dispatch.ResolveAsync(serial, commandId, request, cancellationToken);

        // Always 202, even for an id that resolved nothing. A node retrying a
        // post it never saw acknowledged is the common cause, and answering 404
        // would make it retry a request that can never succeed.
        return Accepted();
    }
}
