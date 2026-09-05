namespace LoraFleet.Domain.Devices;

/// <summary>
/// One bit of the POST mask, and what the firmware does when that block fails.
/// The reactions are copied from the firmware's own table so the dashboard tells
/// an operator the same story the device would.
/// </summary>
public sealed record PostBlock(int Bit, string Name, bool IsCritical, string Reaction)
{
    public int Mask => 1 << Bit;

    /// <summary>Mirrors POST_ITEMS in src/core/post.cpp. Bit numbers are protocol: they travel in the health frame.</summary>
    public static readonly IReadOnlyList<PostBlock> All =
    [
        new(0, "power", true,
            "Sense broken: readings untrusted and the flash-write gate opens. Pack flat: telemetry only."),
        new(1, "nvs", false,
            "Running on firmware defaults. The stored settings were lost, and the operator has to know."),
        new(2, "adc", false,
            "Battery reading untrusted, low-power write gate disabled."),
        new(3, "radio", true,
            "Three init attempts, then degrade. The shell and telemetry stay up."),
        new(4, "display", false,
            "Running headless. UART and radio are unaffected."),
        new(5, "button", false,
            "Key ignored; the node stays receive-only."),
    ];

    public static IEnumerable<PostBlock> Failing(int mask) =>
        All.Where(block => (mask & block.Mask) != 0);

    public static bool HasCriticalFailure(int mask) =>
        All.Any(block => block.IsCritical && (mask & block.Mask) != 0);

    public static string Describe(int mask) =>
        mask == 0 ? "OK" : string.Join(", ", Failing(mask).Select(block => block.Name));
}
