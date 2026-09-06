#include "app/safe_mode.h"

#include <Preferences.h>

#include "core/log.h"

static constexpr uint8_t SAFE_MODE_THRESHOLD = 3;
static constexpr uint32_t CLEAN_UPTIME_MS = 10UL * 60UL * 1000UL;

static constexpr const char *NS = "boot";
static constexpr const char *KEY_ABNORMAL = "abnormal";
static constexpr const char *KEY_TOTAL = "total";

// RTC memory survives a reset but is indeterminate after a power cut, so the
// magic word is what tells the two apart.
static constexpr uint32_t RTC_MAGIC = 0x4C51424Du; // "LQBM"

RTC_NOINIT_ATTR static uint32_t s_rtcMagic;
RTC_NOINIT_ATTR static uint8_t s_rtcAbnormal;

// RTC memory only, deliberately not mirrored into NVS. A board that is browning
// out is the worst possible place to be writing flash, and the counter is only
// meaningful within one run of power anyway: the question it answers is "has
// the supply already failed since it was last connected", and a power cycle is
// exactly the event that makes the answer no.
RTC_NOINIT_ATTR static uint8_t s_rtcBrownouts;

static uint8_t s_brownouts = 0;

static uint8_t s_abnormal = 0;
static uint32_t s_total = 0;
static bool s_safeMode = false;
static bool s_counterCleared = false;
static esp_reset_reason_t s_reason = ESP_RST_UNKNOWN;

// A reset the firmware did not ask for. A clean power cycle, a `reboot`
// command or a flash upload are all normal and must not push the node towards
// safe mode.
static bool isAbnormal(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

static void persistCounters() {
  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return;
  prefs.putUChar(KEY_ABNORMAL, s_abnormal);
  prefs.putULong(KEY_TOTAL, s_total);
  prefs.end();
}

void safeModeBegin() {
  s_reason = esp_reset_reason();

  Preferences prefs;
  if (prefs.begin(NS, /*readOnly=*/true)) {
    s_abnormal = prefs.getUChar(KEY_ABNORMAL, 0);
    s_total = prefs.getULong(KEY_TOTAL, 0);
    prefs.end();
  }

  // RTC memory is the faster and more trustworthy of the two when it is valid:
  // it cannot have been left stale by a write that never reached flash.
  if (s_rtcMagic == RTC_MAGIC) {
    s_abnormal = s_rtcAbnormal;
    s_brownouts = s_rtcBrownouts;
  } else {
    s_rtcMagic = RTC_MAGIC;
    s_rtcAbnormal = s_abnormal;
    s_brownouts = 0;
  }

  s_total++;

  if (isAbnormal(s_reason)) {
    if (s_abnormal < 255) s_abnormal++;
  } else if (s_reason == ESP_RST_POWERON) {
    // A deliberate power cycle is the field's universal first repair, and it
    // deserves to work: it clears the streak.
    s_abnormal = 0;
  }

  if (s_reason == ESP_RST_BROWNOUT) {
    if (s_brownouts < 255) s_brownouts++;
  } else if (s_reason == ESP_RST_POWERON) {
    // Somebody has been at the hardware. Give the supply the benefit of the
    // doubt: they may well have plugged in the battery this counter exists to
    // ask for.
    s_brownouts = 0;
  }

  s_rtcAbnormal = s_abnormal;
  s_rtcBrownouts = s_brownouts;
  persistCounters();

  s_safeMode = (s_abnormal >= SAFE_MODE_THRESHOLD);

  LOG_I(TAG_SYS, E_BOOT, (uint32_t)s_reason);
  LOG_I(TAG_SYS, E_BOOT_COUNT, s_abnormal);
  if (s_safeMode) LOG_E(TAG_SYS, E_SAFE_MODE, s_abnormal);
}

void safeModeTick() {
  if (s_counterCleared || (s_abnormal == 0 && s_brownouts == 0)) return;
  if (millis() < CLEAN_UPTIME_MS) return;

  s_abnormal = 0;
  s_rtcAbnormal = 0;
  s_brownouts = 0;
  s_rtcBrownouts = 0;
  s_counterCleared = true;
  persistCounters();

  LOG_I(TAG_SYS, E_UPTIME_CLEAN, millis() / 1000u);
}

bool safeModeActive() { return s_safeMode; }
uint8_t abnormalBootCount() { return s_abnormal; }
uint8_t brownoutStreak() { return s_brownouts; }
uint32_t totalBootCount() { return s_total; }
esp_reset_reason_t lastResetReason() { return s_reason; }

const char *lastResetReasonName() {
  switch (s_reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

uint8_t lastCrashCode() { return (uint8_t)s_reason; }
