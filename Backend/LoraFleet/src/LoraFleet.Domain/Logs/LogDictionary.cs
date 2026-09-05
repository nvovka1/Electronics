using LoraFleet.Domain.Devices;

namespace LoraFleet.Domain.Logs;

/// <summary>
/// The backend half of docs/log_dict.csv. A node in the field sends numbers, so
/// without this a dump is a column of integers.
/// </summary>
/// <remarks>
/// These numbers are a contract with the firmware's LogCode enum. Changing one
/// here without changing it there - or the other way round - makes every older
/// dump decode to the wrong story, silently. The firmware's own copy is
/// generated from the enum by scripts/gen_log_dict.py for exactly that reason.
/// </remarks>
public static class LogDictionary
{
    private static readonly Dictionary<byte, string> Levels = new()
    {
        [0] = "PANIC", [1] = "ERROR", [2] = "WARN",
        [3] = "INFO", [4] = "DEBUG", [5] = "TRACE",
    };

    private static readonly Dictionary<byte, string> Tags = new()
    {
        [0] = "sys", [1] = "post", [2] = "cfg", [3] = "radio",
        [4] = "ui", [5] = "key", [6] = "batt",
    };

    private static readonly Dictionary<byte, string> Codes = new()
    {
        [0] = "none",
        [1] = "boot",
        [2] = "boot_count",
        [3] = "fw_dirty",
        [4] = "safe_mode",
        [5] = "task_start_fail",
        [6] = "uptime_clean",
        [7] = "wdt_subscribed",
        [10] = "post_fail",
        [11] = "post_pass",
        [12] = "post_mask",
        [13] = "post_critical",
        [20] = "cfg_loaded",
        [21] = "cfg_defaults",
        [22] = "cfg_migrated",
        [23] = "cfg_changed",
        [24] = "cfg_saved",
        [25] = "cfg_save_fail",
        [26] = "cfg_slot_bad",
        [27] = "cfg_reset",
        [28] = "cfg_invalid",
        [30] = "radio_init_fail",
        [31] = "tx",
        [32] = "rx",
        [33] = "tx_noack",
        [34] = "rx_bad_frame",
        [35] = "rx_dup",
        [36] = "ack_rx",
        [37] = "health_tx",
        [38] = "radio_ready",
        [39] = "tx_airtime",
        [40] = "lowbat_write_blocked",
        [41] = "batt_low",
        [42] = "batt_untrusted",
        [50] = "queue_full",
    };

    private static readonly Dictionary<int, string> ResetReasons = new()
    {
        [0] = "UNKNOWN", [1] = "POWERON", [2] = "EXT", [3] = "SW",
        [4] = "PANIC", [5] = "INT_WDT", [6] = "TASK_WDT", [7] = "WDT",
        [8] = "DEEPSLEEP", [9] = "BROWNOUT", [10] = "SDIO",
    };

    private static readonly Dictionary<int, string> FrameErrors = new()
    {
        [0] = "ok", [1] = "bad argument", [2] = "too short", [3] = "bad sync",
        [4] = "bad version", [5] = "bad length", [6] = "crc mismatch",
        [7] = "unknown type",
    };

    public static string LevelName(byte level) =>
        Levels.TryGetValue(level, out var name) ? name : $"?{level}";

    public static string TagName(byte tag) =>
        Tags.TryGetValue(tag, out var name) ? name : $"?{tag}";

    public static string CodeName(byte code) =>
        Codes.TryGetValue(code, out var name) ? name : $"unknown({code})";

    public static string ResetReasonName(int reason) =>
        ResetReasons.TryGetValue(reason, out var name) ? name : $"UNKNOWN({reason})";

    /// <summary>
    /// Renders the argument the way that code's comment in log.h documents it.
    /// Several codes pack two values into one word, and showing those raw turns
    /// a readable event into a nine-digit number.
    /// </summary>
    public static string DescribeArg(byte code, long arg) => code switch
    {
        1 => ResetReasonName((int)arg),                                 // boot
        4 => $"{arg} abnormal boots",                                   // safe_mode
        7 => $"{arg} s",                                                // wdt_subscribed
        10 or 13 => PostBlockName((int)arg),                            // post_fail, post_critical
        12 => $"0x{arg:X4} ({PostBlock.Describe((int)arg)})",           // post_mask
        20 or 24 => $"slot {SlotName(arg)} seq {arg & 0xFFFFFF}",       // cfg_loaded, cfg_saved
        22 => $"v{(arg >> 16) & 0xFFFF} -> v{arg & 0xFFFF}",            // cfg_migrated
        26 => $"slot {SlotFromIndex(arg)}",                             // cfg_slot_bad
        32 or 35 => $"src {(arg >> 16) & 0xFFFF} seq {arg & 0xFFFF}",   // rx, rx_dup
        34 => FrameErrorName((int)arg),                                 // rx_bad_frame
        36 => $"{(sbyte)arg} dBm",                                      // ack_rx
        39 => $"{arg} ms",                                              // tx_airtime
        40 or 41 => $"{arg} mV",                                        // lowbat_write_blocked, batt_low
        _ => arg.ToString(),
    };

    /// <summary>cfg_loaded and cfg_saved pack the slot index into the top byte.</summary>
    private static string SlotName(long packed) => SlotFromIndex((packed >> 24) & 0xFF);

    /// <summary>cfg_slot_bad carries the bare slot index instead.</summary>
    private static string SlotFromIndex(long index) => index switch
    {
        0 => "A",
        1 => "B",
        _ => "-",
    };

    private static string PostBlockName(int bit) =>
        PostBlock.All.FirstOrDefault(block => block.Bit == bit)?.Name ?? $"bit {bit}";

    private static string FrameErrorName(int result) =>
        FrameErrors.TryGetValue(result, out var name) ? name : $"result {result}";
}
