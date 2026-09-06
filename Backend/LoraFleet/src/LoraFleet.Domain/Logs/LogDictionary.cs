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
        [4] = "ui", [5] = "key", [6] = "batt", [7] = "net", [8] = "ota",
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
        [60] = "net_cfg_loaded",
        [61] = "net_cfg_changed",
        [62] = "wifi_connecting",
        [63] = "wifi_up",
        [64] = "wifi_down",
        [65] = "wifi_fail",
        [66] = "time_synced",
        [67] = "report_ok",
        [68] = "report_fail",
        [69] = "logs_sent",
        [70] = "logs_fail",
        [71] = "tls_insecure",
        [72] = "net_unprovisioned",
        [73] = "net_disabled",
        [74] = "net_tx_power",
        [75] = "net_brownout_hold",
        [76] = "net_no_api_key",
        [80] = "ota_check",
        [81] = "ota_available",
        [82] = "ota_refused",
        [83] = "ota_begin",
        [84] = "ota_progress",
        [85] = "ota_hash_mismatch",
        [86] = "ota_write_fail",
        [87] = "ota_download_fail",
        [88] = "ota_staged",
        [89] = "ota_trial",
        [90] = "ota_confirmed",
        [91] = "ota_rollback",
        [92] = "ota_blocked",
    };

    /// <summary>
    /// Which precondition refused an update. Mirrors ota_gate_t in the
    /// firmware's src/net/ota.h - the numbers are positional, so inserting one
    /// there without inserting it here renames every refusal after it.
    /// </summary>
    private static readonly Dictionary<int, string> OtaGates = new()
    {
        [0] = "ok",
        [1] = "ota_enabled is 0",
        [2] = "this image is still on trial itself",
        [3] = "battery below ota_vbat_min_mv",
        [4] = "the ADC self-test failed, so the battery reading means nothing",
        [5] = "the image is built for a different board",
        [6] = "already running this version",
        [7] = "the image does not fit the inactive slot",
        [8] = "not enough free heap",
        [9] = "this version already failed its trial on this node",
        [10] = "no usable manifest",
    };

    private static readonly Dictionary<int, string> OtaRollbackReasons = new()
    {
        [1] = "never checked in during the trial window",
        [2] = "a critical POST block failed on the new image",
        [3] = "an operator asked for it",
    };

    /// <summary>
    /// The credential fields, in the order netcfg_field_t declares them. Only
    /// the index is ever logged - two of the four are secrets and the ring log
    /// leaves the device.
    /// </summary>
    private static readonly Dictionary<int, string> NetCfgFields = new()
    {
        [0] = "ssid", [1] = "password", [2] = "base url", [3] = "api key",
    };

    /// <summary>WiFi.status(), which is wl_status_t in the Arduino core.</summary>
    private static readonly Dictionary<int, string> WifiStatus = new()
    {
        [0] = "idle", [1] = "no ssid available", [2] = "scan completed",
        [3] = "connected", [4] = "connect failed", [5] = "connection lost",
        [6] = "disconnected",
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
        42 => arg == 0 ? "no reading taken" : $"{arg} mV, implausible",  // batt_untrusted
        60 => arg == 1 ? "provisioned" : "no ssid or no base url",      // net_cfg_loaded
        61 => NetCfgFieldName((int)arg),                                // net_cfg_changed
        62 or 65 => $"attempt {arg}",                                   // wifi_connecting, wifi_fail
        63 => DescribeIPv4(arg),                                        // wifi_up
        64 => WifiStatusName((int)arg),                                 // wifi_down
        66 => DescribeUnixSeconds(arg),                                 // time_synced
        67 or 68 or 70 or 80 => DescribeHttpStatus(arg),                // report/log/ota http results
        69 => $"{arg} records",                                         // logs_sent
        74 => $"{arg} dBm",                                             // net_tx_power
        75 => $"{arg} brownout resets - uplink held off",               // net_brownout_hold
        76 => "no api key set on the node",                              // net_no_api_key
        81 or 83 or 88 => $"{arg:N0} bytes",                            // ota_available/begin/staged
        82 => OtaGateName((int)arg),                                    // ota_refused
        84 => $"{arg}%",                                                // ota_progress
        85 or 87 => $"{arg:N0} bytes in",                               // hash mismatch, download fail
        90 => $"after {arg} s of uptime",                               // ota_confirmed
        91 => OtaRollbackReasonName((int)arg),                          // ota_rollback
        _ => arg.ToString(),
    };

    /// <summary>
    /// The firmware sends its own negative client errors as well as HTTP
    /// statuses, and -1 rendered bare is the least useful thing in a log.
    /// </summary>
    private static string DescribeHttpStatus(long arg) => arg switch
    {
        204 => "204 nothing to do",
        >= 200 and < 300 => $"{arg} ok",
        -1000 => "no link, no credentials, or no clock for TLS",
        -1001 => "unusable response",
        -1 => "connection refused",
        -5 => "connection lost",
        -11 => "read timeout",
        < 0 => $"client error {arg}",
        _ => $"HTTP {arg}",
    };

    /// <summary>
    /// The node has no RTC, so this is the one log line that carries real
    /// wall-clock time: the moment SNTP first answered.
    /// </summary>
    private static string DescribeUnixSeconds(long arg) =>
        arg <= 0 ? "not synced" : DateTimeOffset.FromUnixTimeSeconds(arg).UtcDateTime.ToString("u");

    private static string DescribeIPv4(long arg) =>
        $"{arg & 0xFF}.{(arg >> 8) & 0xFF}.{(arg >> 16) & 0xFF}.{(arg >> 24) & 0xFF}";

    private static string NetCfgFieldName(int index) =>
        NetCfgFields.TryGetValue(index, out var name) ? name : $"field {index}";

    private static string WifiStatusName(int status) =>
        WifiStatus.TryGetValue(status, out var name) ? name : $"status {status}";

    private static string OtaGateName(int gate) =>
        OtaGates.TryGetValue(gate, out var name) ? name : $"gate {gate}";

    private static string OtaRollbackReasonName(int reason) =>
        OtaRollbackReasons.TryGetValue(reason, out var name) ? name : $"reason {reason}";

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
