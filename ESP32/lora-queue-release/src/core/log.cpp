#include "core/log.h"

// 256 records is 3 KB of RAM and holds several minutes of a busy node, which
// is what a crash dump needs. The flash is deliberately left alone: a sector
// is good for about 100k cycles, and a record per second would wear one out in
// six weeks.
static constexpr uint16_t LOG_CAPACITY = 256;

static log_rec_t s_storage[LOG_CAPACITY];
static log_ring_t s_ring;
static uint8_t s_level = LVL_INFO;

// A spinlock rather than a mutex: this is taken from task context and from
// interrupt context, and a mutex cannot be taken from an ISR.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void logBegin() {
  ring_init(&s_ring, s_storage, LOG_CAPACITY);
  s_level = LVL_INFO;
}

void logSetLevel(uint8_t level) {
  if (level > LVL_TRACE) level = LVL_TRACE;
  s_level = level;
}

uint8_t logGetLevel() { return s_level; }

void logPut(uint8_t level, uint8_t tag, uint8_t code, uint32_t arg) {
  if (level > s_level) return;

  const log_rec_t rec = {millis(), level, tag, code, 0, arg};

  portENTER_CRITICAL_SAFE(&s_lock);
  ring_push(&s_ring, &rec);
  portEXIT_CRITICAL_SAFE(&s_lock);

#if LOG_TO_UART
  // Only ever from task context. Formatting a line and pushing it into a
  // blocking UART takes milliseconds; doing that from an interrupt would stop
  // everything, so from an ISR the ring entry is the whole of the log.
  if (!xPortInIsrContext()) {
    Serial.printf("[%8lu] %-5s %-5s %-22s %lu\n", (unsigned long)rec.ts_ms,
                  logLevelName(level), logTagName(tag), logCodeName(code),
                  (unsigned long)arg);
  }
#endif
}

uint16_t logCount() {
  portENTER_CRITICAL_SAFE(&s_lock);
  const uint16_t n = ring_count(&s_ring);
  portEXIT_CRITICAL_SAFE(&s_lock);
  return n;
}

uint32_t logDropped() {
  portENTER_CRITICAL_SAFE(&s_lock);
  const uint32_t n = ring_dropped(&s_ring);
  portEXIT_CRITICAL_SAFE(&s_lock);
  return n;
}

uint32_t logPushed() {
  portENTER_CRITICAL_SAFE(&s_lock);
  const uint32_t n = s_ring.pushed;
  portEXIT_CRITICAL_SAFE(&s_lock);
  return n;
}

bool logPeek(uint16_t index, log_rec_t &out) {
  portENTER_CRITICAL_SAFE(&s_lock);
  const bool ok = ring_peek_newest(&s_ring, index, &out);
  portEXIT_CRITICAL_SAFE(&s_lock);
  return ok;
}

void logClear() {
  portENTER_CRITICAL_SAFE(&s_lock);
  ring_clear(&s_ring);
  portEXIT_CRITICAL_SAFE(&s_lock);
}

const char *logLevelName(uint8_t level) {
  switch (level) {
    case LVL_PANIC: return "PANIC";
    case LVL_ERROR: return "ERROR";
    case LVL_WARN: return "WARN";
    case LVL_INFO: return "INFO";
    case LVL_DEBUG: return "DEBUG";
    case LVL_TRACE: return "TRACE";
    default: return "?";
  }
}

const char *logTagName(uint8_t tag) {
  switch (tag) {
    case TAG_SYS: return "sys";
    case TAG_POST: return "post";
    case TAG_CFG: return "cfg";
    case TAG_RADIO: return "radio";
    case TAG_UI: return "ui";
    case TAG_KEY: return "key";
    case TAG_BATT: return "batt";
    case TAG_NET: return "net";
    case TAG_OTA: return "ota";
    default: return "?";
  }
}

const char *logCodeName(uint8_t code) {
  switch (code) {
    case E_NONE: return "none";
    case E_BOOT: return "boot";
    case E_BOOT_COUNT: return "boot_count";
    case E_FW_DIRTY: return "fw_dirty";
    case E_SAFE_MODE: return "safe_mode";
    case E_TASK_START_FAIL: return "task_start_fail";
    case E_UPTIME_CLEAN: return "uptime_clean";
    case E_WDT_SUBSCRIBED: return "wdt_subscribed";
    case E_POST_FAIL: return "post_fail";
    case E_POST_PASS: return "post_pass";
    case E_POST_MASK: return "post_mask";
    case E_POST_CRITICAL: return "post_critical";
    case E_CFG_LOADED: return "cfg_loaded";
    case E_CFG_DEFAULTS: return "cfg_defaults";
    case E_CFG_MIGRATED: return "cfg_migrated";
    case E_CFG_CHANGED: return "cfg_changed";
    case E_CFG_SAVED: return "cfg_saved";
    case E_CFG_SAVE_FAIL: return "cfg_save_fail";
    case E_CFG_SLOT_BAD: return "cfg_slot_bad";
    case E_CFG_RESET: return "cfg_reset";
    case E_CFG_INVALID: return "cfg_invalid";
    case E_RADIO_INIT_FAIL: return "radio_init_fail";
    case E_TX: return "tx";
    case E_RX: return "rx";
    case E_TX_NOACK: return "tx_noack";
    case E_RX_BAD_FRAME: return "rx_bad_frame";
    case E_RX_DUP: return "rx_dup";
    case E_ACK_RX: return "ack_rx";
    case E_HEALTH_TX: return "health_tx";
    case E_RADIO_READY: return "radio_ready";
    case E_TX_AIRTIME: return "tx_airtime";
    case E_LOWBAT_WRITE_BLOCKED: return "lowbat_write_blocked";
    case E_BATT_LOW: return "batt_low";
    case E_BATT_UNTRUSTED: return "batt_untrusted";
    case E_QUEUE_FULL: return "queue_full";
    case E_NET_CFG_LOADED: return "net_cfg_loaded";
    case E_NET_CFG_CHANGED: return "net_cfg_changed";
    case E_WIFI_CONNECTING: return "wifi_connecting";
    case E_WIFI_UP: return "wifi_up";
    case E_WIFI_DOWN: return "wifi_down";
    case E_WIFI_FAIL: return "wifi_fail";
    case E_TIME_SYNCED: return "time_synced";
    case E_REPORT_OK: return "report_ok";
    case E_REPORT_FAIL: return "report_fail";
    case E_LOGS_SENT: return "logs_sent";
    case E_LOGS_FAIL: return "logs_fail";
    case E_TLS_INSECURE: return "tls_insecure";
    case E_NET_UNPROVISIONED: return "net_unprovisioned";
    case E_NET_DISABLED: return "net_disabled";
    case E_NET_TX_POWER: return "net_tx_power";
    case E_NET_BROWNOUT_HOLD: return "net_brownout_hold";
    case E_NET_NO_API_KEY: return "net_no_api_key";
    case E_NET_BAD_JSON: return "net_bad_json";
    case E_OTA_CHECK: return "ota_check";
    case E_OTA_AVAILABLE: return "ota_available";
    case E_OTA_REFUSED: return "ota_refused";
    case E_OTA_BEGIN: return "ota_begin";
    case E_OTA_PROGRESS: return "ota_progress";
    case E_OTA_HASH_MISMATCH: return "ota_hash_mismatch";
    case E_OTA_WRITE_FAIL: return "ota_write_fail";
    case E_OTA_DOWNLOAD_FAIL: return "ota_download_fail";
    case E_OTA_STAGED: return "ota_staged";
    case E_OTA_TRIAL: return "ota_trial";
    case E_OTA_CONFIRMED: return "ota_confirmed";
    case E_OTA_ROLLBACK: return "ota_rollback";
    case E_OTA_BLOCKED: return "ota_blocked";
    case E_OTA_VERSION_MISMATCH: return "ota_version_mismatch";
    default: return "unknown";
  }
}

void logDump(Print &out, uint16_t count) {
  const uint16_t held = logCount();
  if (count == 0 || count > held) count = held;

  out.printf("log: %u of %u records, %lu dropped, level %s\n", count, held,
             (unsigned long)logDropped(), logLevelName(s_level));

  if (count == 0) return;
  out.println("      ts_ms  level tag   code                    arg");

  // Oldest of the requested window first, so the dump reads forward in time
  // even though the ring is walked backwards.
  for (int16_t i = (int16_t)count - 1; i >= 0; i--) {
    log_rec_t r;
    if (!logPeek((uint16_t)i, r)) continue;
    out.printf("%11lu  %-5s %-5s %-22s %lu\n", (unsigned long)r.ts_ms,
               logLevelName(r.lvl), logTagName(r.tag), logCodeName(r.code),
               (unsigned long)r.arg);
  }
}
