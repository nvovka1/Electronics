using Initiator.Domain.States;

namespace Initiator.Web;

/// <summary>
/// Small formatting helpers the views share. Kept out of the view models
/// because they are presentation, and out of the domain because they are not
/// rules.
/// </summary>
public static class Describe
{
    /// <summary>
    /// A duration at the precision a human actually wants: seconds up to a
    /// minute, then minutes, then hours, then days. Nobody reads "4823 seconds".
    /// </summary>
    public static string Duration(TimeSpan span)
    {
        if (span < TimeSpan.Zero) span = TimeSpan.Zero;

        if (span.TotalSeconds < 60) return $"{(int)span.TotalSeconds}s";
        if (span.TotalMinutes < 60) return $"{(int)span.TotalMinutes}m";
        if (span.TotalHours < 48) return $"{(int)span.TotalHours}h";

        return $"{(int)span.TotalDays}d";
    }

    /// <summary>A countdown as m:ss, which is how a countdown is read.</summary>
    public static string Countdown(int seconds)
    {
        if (seconds <= 0) return "due now";

        var span = TimeSpan.FromSeconds(seconds);
        return span.TotalMinutes >= 1
            ? $"{(int)span.TotalMinutes}:{span.Seconds:00}"
            : $"{span.Seconds}s";
    }

    /// <summary>Upper case, because these read as labels on a control rather than as words.</summary>
    public static string Command(CommandType command) => command.ToString().ToUpperInvariant();

    public static string State(NodeState state) => state.ToString().ToUpperInvariant();

    /// <summary>
    /// Where a transition came from, in words rather than an enum name: "the
    /// controller" is what the operator calls the handheld.
    /// </summary>
    public static string Source(CommandSource source) => source switch
    {
        CommandSource.Lora => "controller",
        CommandSource.Api => "dashboard",
        CommandSource.Timer => "auto-arm",
        _ => source.ToString().ToLowerInvariant(),
    };

    /// <summary>
    /// A transition in one line. A rejection reads as one, rather than as a row
    /// where you have to notice a false in a column.
    /// </summary>
    public static string Transition(StateEvent stateEvent)
    {
        ArgumentNullException.ThrowIfNull(stateEvent);

        if (stateEvent.IsAutoArm)
            return "countdown expired → ARMED";

        var command = stateEvent.Command is null
            ? "(no command)"
            : Command(stateEvent.Command.Value);

        if (!stateEvent.Accepted)
            return $"{command} refused — {Reason(stateEvent.Reason)}";

        return stateEvent.StateChanged
            ? $"{command} → {State(stateEvent.ToState)}"
            : $"{command} (already {State(stateEvent.ToState)})";
    }

    public static string Reason(RejectReason reason) => reason switch
    {
        RejectReason.Ok => "accepted",
        RejectReason.BadTransition => "not allowed from that state",
        RejectReason.BadTarget => "addressed to another node",
        RejectReason.Replay => "already seen that counter",
        RejectReason.BadCommand => "command not understood",
        _ => reason.ToString(),
    };
}
