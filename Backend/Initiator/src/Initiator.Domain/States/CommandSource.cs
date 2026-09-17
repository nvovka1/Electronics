namespace Initiator.Domain.States;

/// <summary>Where a transition came from.</summary>
public enum CommandSource
{
    /// <summary>A frame from the handheld controller.</summary>
    Lora,

    /// <summary>Queued on the dashboard and collected by the node on its next poll.</summary>
    Api,

    /// <summary>
    /// The auto-arm. No command and nobody behind it: the node's INIT countdown
    /// expired and it armed itself.
    /// </summary>
    Timer,
}
