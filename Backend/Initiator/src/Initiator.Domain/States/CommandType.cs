namespace Initiator.Domain.States;

/// <summary>
/// The four commands a node accepts. The numbers travel in the LoRa frame's
/// <c>command</c> byte and must match the firmware's enum.
/// </summary>
/// <remarks>
/// Deliberately starts at 1. Zero is what an uninitialised byte and a truncated
/// payload both look like, and neither should decode to a valid command.
/// </remarks>
public enum CommandType : byte
{
    Init = 1,
    Arm = 2,
    Fire = 3,
    Safe = 4,
}
