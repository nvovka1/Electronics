using Initiator.Domain.States;

namespace Initiator.Web.Infrastructure;

/// <summary>
/// Turns the raw bytes a node sends into domain enums, refusing anything it does
/// not recognise.
/// </summary>
/// <remarks>
/// A plain cast from byte to enum succeeds for every value in the range,
/// including the ones with no meaning. That would put a <c>(NodeState)200</c>
/// into the database and onto the dashboard, and the first place it would be
/// noticed is a page that renders "state200". Every value off the wire goes
/// through here instead.
/// </remarks>
public static class WireValues
{
    public static bool TryState(byte value, out NodeState state)
    {
        state = (NodeState)value;
        return Enum.IsDefined(state);
    }

    public static bool TryCommand(byte value, out CommandType command)
    {
        command = (CommandType)value;
        return Enum.IsDefined(command);
    }

    public static bool TrySource(byte value, out CommandSource source)
    {
        source = (CommandSource)value;
        return Enum.IsDefined(source);
    }

    /// <summary>
    /// A reason code, falling back to <see cref="RejectReason.Ok"/>. Unlike the
    /// others this one does not fail the request: a reason this build does not
    /// know is a newer firmware explaining itself, and losing the explanation is
    /// not worth discarding the event it came with.
    /// </summary>
    public static RejectReason Reason(byte value) =>
        Enum.IsDefined((RejectReason)value) ? (RejectReason)value : RejectReason.Ok;
}
