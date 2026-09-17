using Initiator.Domain.States;

namespace Initiator.Domain.Logs;

/// <summary>
/// The backend half of the firmware's log codes. A node sends numbers, so
/// without this a dump is a column of integers.
/// </summary>
/// <remarks>
/// <para>
/// These numbers are a contract with the firmware's <c>LogCode</c> enum in
/// <c>ESP32/initiator/explosion</c>. Change one here without changing it there —
/// or the other way round — and every stored record decodes to the wrong story,
/// silently, which is the worst way for it to happen.
/// </para>
/// <para>
/// Codes are grouped in tens by tag, with gaps, so a new code can be added next
/// to its relatives instead of on the end. Renumbering an existing one is never
/// correct: records already stored carry the old number.
/// </para>
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
        [0] = "sys",
        [1] = "post",
        [2] = "cfg",
        [3] = "radio",
        [4] = "state",
        [5] = "led",
        [6] = "ui",
        [7] = "net",
        [8] = "batt",
    };

    private static readonly Dictionary<byte, string> Codes = new()
    {
        [0] = "none",

        // sys
        [1] = "boot",
        [2] = "boot_count",
        [3] = "fw_dirty",
        [4] = "task_start_fail",
        [5] = "wdt_subscribed",

        // post
        [10] = "post_pass",
        [11] = "post_fail",
        [12] = "post_mask",

        // cfg
        [20] = "cfg_loaded",
        [21] = "cfg_defaults",
        [22] = "cfg_saved",
        [23] = "cfg_save_fail",
        [24] = "cfg_changed",

        // radio
        [30] = "radio_ready",
        [31] = "radio_init_fail",
        [32] = "cmd_rx",
        [33] = "cmd_not_for_us",
        [34] = "cmd_replay",
        [35] = "rx_bad_frame",
        [36] = "ack_tx",
        [37] = "announce_tx",
        [38] = "cmd_tx",
        [39] = "ack_rx",
        [40] = "tx_noack",

        // state
        [50] = "state_enter",
        [51] = "cmd_accepted",
        [52] = "cmd_rejected",
        [53] = "countdown_started",
        [54] = "countdown_restarted",
        [55] = "countdown_cancelled",
        [56] = "auto_arm",

        // net
        [70] = "wifi_up",
        [71] = "wifi_down",
        [72] = "report_ok",
        [73] = "report_fail",
        [74] = "cmd_polled",
        [75] = "event_posted",
        [76] = "event_buffered",
        [77] = "log_sent",

        // batt
        [90] = "batt_low",
        [91] = "batt_untrusted",
    };

    public static string LevelName(byte level) =>
        Levels.TryGetValue(level, out var name) ? name : $"L{level}";

    public static string TagName(byte tag) =>
        Tags.TryGetValue(tag, out var name) ? name : $"tag{tag}";

    public static string CodeName(byte code) =>
        Codes.TryGetValue(code, out var name) ? name : $"code{code}";

    /// <summary>
    /// Renders a code's argument the way that code documents it, so a packed
    /// value reads as what it means rather than as a large integer.
    /// </summary>
    public static string DescribeArg(byte code, long arg) => CodeName(code) switch
    {
        "none" => string.Empty,

        // The state codes carry a NodeState, which is worth naming: "state_enter
        // 2" is a lookup every single time, and "state_enter ARMED" is not.
        "state_enter" or "auto_arm" => StateName(arg),

        // Packed: the low byte is the command, the next the resulting state.
        // One record rather than two, because two can be split by a ring wrap.
        "cmd_accepted" => $"{CommandName(arg & 0xFF)} -> {StateName((arg >> 8) & 0xFF)}",
        "cmd_rejected" => $"{CommandName(arg & 0xFF)} refused ({ReasonName((arg >> 8) & 0xFF)})",

        "cmd_rx" or "cmd_tx" => CommandName(arg),
        "cmd_not_for_us" => $"addressed to node {arg}",
        "cmd_replay" => $"counter {arg}",

        "countdown_started" or "countdown_restarted" => $"{arg} s",
        "countdown_cancelled" => $"{arg} s left",

        "ack_rx" or "tx_noack" => $"attempt {arg}",
        "announce_tx" => $"repeat {arg}",

        "post_mask" => arg == 0 ? "clean" : $"0x{arg:X2}",
        "boot_count" => $"{arg} boots",
        "batt_low" or "batt_untrusted" => $"{arg / 10.0:0.0} V",

        "report_fail" => $"HTTP {arg}",
        "event_buffered" => $"{arg} waiting",

        _ => arg == 0 ? string.Empty : arg.ToString(),
    };

    private static string StateName(long value) =>
        Enum.IsDefined(typeof(NodeState), (byte)value)
            ? ((NodeState)(byte)value).ToString().ToUpperInvariant()
            : $"state{value}";

    private static string CommandName(long value) =>
        Enum.IsDefined(typeof(CommandType), (byte)value)
            ? ((CommandType)(byte)value).ToString().ToUpperInvariant()
            : $"command{value}";

    private static string ReasonName(long value) =>
        Enum.IsDefined(typeof(RejectReason), (byte)value)
            ? ((RejectReason)(byte)value).ToString()
            : $"reason{value}";

    /// <summary>Every known tag, for the filter drop-down.</summary>
    public static IReadOnlyDictionary<byte, string> AllTags => Tags;

    /// <summary>Every known level, for the filter drop-down.</summary>
    public static IReadOnlyDictionary<byte, string> AllLevels => Levels;
}
