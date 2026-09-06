#include "net/net_task.h"

#include <WiFi.h>
#include <time.h>

#include "app/safe_mode.h"
#include "core/config.h"
#include "core/log.h"
#include "net/netcfg.h"
#include "net/fleet_client.h"
#include "net/ota.h"
#include "tasks/radio_task.h"
#include "tasks/ui_task.h"

// Records per POST. Thirty-two twelve-byte records make a body of about two
// kilobytes, which fits comfortably in one TLS session without a large heap
// allocation. The rest go in the next cycle: the ring holds 256, so nothing is
// lost unless the link has been down long enough to overwrite them - and when
// that happens the gap is logged rather than quietly closed.
static constexpr uint16_t LOG_BATCH_MAX = 32;

// Two brownouts and the uplink stays down until somebody power-cycles the
// board - which is also the gesture that most often means they have just fitted
// the battery or changed the cable.
//
// Two rather than three because each attempt costs a reboot, and the second
// failure already tells us what the first one did: the supply cannot do this.
// The node is more useful spending that third boot answering questions.
static constexpr uint8_t BROWNOUT_HOLD_AT = 2;

// After a brownout, let the rails settle and the rest of start-up finish before
// keying the transmitter again. The OLED, the LoRa module and the flash are all
// drawing during the first couple of seconds of a boot.
static constexpr uint32_t BROWNOUT_SETTLE_MS = 5000;

static constexpr uint32_t ASSOCIATE_TIMEOUT_MS = 20000;
static constexpr uint32_t BACKOFF_MIN_MS = 30000;
static constexpr uint32_t BACKOFF_MAX_MS = 300000;

// 2023-01-01. Any clock below this is the ESP32's power-on epoch, not a date.
static constexpr time_t SANE_EPOCH = 1672531200;

static bool s_timeSynced = false;
static uint32_t s_lastCheckInMs = 0;
static uint32_t s_lastAttemptMs = 0;
static uint32_t s_backoffMs = BACKOFF_MIN_MS;
static uint16_t s_associateFailures = 0;
static uint32_t s_logCursor = 0; // a logPushed() value: everything below is sent
static uint32_t s_logGap = 0;    // records the ring overwrote before we sent them

static char s_targetVersion[24] = {0};
static bool s_updateAvailable = false;

static volatile bool s_reportRequested = false;
static volatile bool s_updateCheckRequested = false;
static volatile bool s_applyRequested = false;

static bool s_warnedUnprovisioned = false;
static bool s_warnedDisabled = false;
static bool s_warnedBrownout = false;
static bool s_warnedNoKey = false;
static int8_t s_appliedDbm = 0;

// --- transmit power -------------------------------------------------------

// wifi_power_t counts in quarter-dBm steps and only a fixed set of levels
// exists, so a requested value is rounded DOWN to a level the radio actually
// has. Rounding down rather than to the nearest matters: every step up is more
// peak current out of the supply, and this whole mechanism exists because that
// current is what knocks the board over.
static wifi_power_t powerLevelFor(uint8_t dbm) {
  static const wifi_power_t LEVELS[] = {
      WIFI_POWER_2dBm,  WIFI_POWER_5dBm,    WIFI_POWER_7dBm,  WIFI_POWER_8_5dBm,
      WIFI_POWER_11dBm, WIFI_POWER_13dBm,   WIFI_POWER_15dBm, WIFI_POWER_17dBm,
      WIFI_POWER_18_5dBm, WIFI_POWER_19dBm, WIFI_POWER_19_5dBm,
  };

  wifi_power_t chosen = LEVELS[0];
  for (size_t i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); i++)
    if ((int)LEVELS[i] <= (int)dbm * 4) chosen = LEVELS[i];

  return chosen;
}

// The level this attempt intends to use. Chosen and LOGGED before the radio is
// touched at all, because of what the field log showed: on a supply that cannot
// carry WiFi, the brownout happens inside WiFi.mode() - when the driver powers
// up the RF front end and calibrates it - and not during transmission. A record
// written after that point is a record that never gets written, and its absence
// from a dump is the clue that says which of the two failures this was.
static uint8_t intendedTxDbm() {
  uint8_t dbm = config().wifi_tx_dbm;

  // The supply has already failed at least once since power was applied. Ask
  // for the least the radio can do rather than the configured value.
  if (brownoutStreak() > 0) dbm = 2;

  LOG_I(TAG_NET, E_NET_TX_POWER, (uint32_t)dbm);
  return dbm;
}

// Applied once the driver is running. This bounds the TRANSMIT burst, which is
// the larger of the two spikes but the later one; nothing here can shrink the
// start-up burst that comes first.
static void applyTxPower(uint8_t dbm) {
  const wifi_power_t level = powerLevelFor(dbm);
  WiFi.setTxPower(level);
  s_appliedDbm = (int8_t)((int)level / 4);
}

// --- association ----------------------------------------------------------

static bool ensureAssociated() {
  if (WiFi.status() == WL_CONNECTED) return true;

  const uint32_t now = millis();
  if (s_lastAttemptMs != 0 && now - s_lastAttemptMs < s_backoffMs) return false;
  s_lastAttemptMs = now;

  LOG_I(TAG_NET, E_WIFI_CONNECTING, s_associateFailures + 1u);
  const uint8_t dbm = intendedTxDbm();

  // Every milliamp something else is not drawing is a milliamp available to the
  // RF front end when it powers up, and that moment is where a marginal supply
  // gives way. Three things stand down together, worth roughly 55 mA out of a
  // burst of about 250:
  //
  //   processor  240 -> 80 MHz          ~30 mA
  //   display    blanked                ~15 mA
  //   LoRa       receive -> sleep       ~12 mA
  //
  // That is not enough to rescue a genuinely bad supply and is not meant to be
  // - a cable that cannot deliver 250 mA usually cannot deliver 195 either. It
  // is enough to matter on a board that is only just failing, and it costs
  // nothing on a healthy one because it is applied only after a brownout has
  // already happened.
  const bool easeOff = brownoutStreak() > 0;
  if (easeOff) {
    uiSuspendPanel();
    radioStandDown();
    Serial.flush(); // the switch reprograms the UART divider mid-character
    setCpuFrequencyMhz(80);
  }

  // Only when there is something to disconnect. On the first attempt of a boot
  // the driver has never been started, and asking it to disconnect is work and
  // an error line for nothing.
  if (WiFi.getMode() != WIFI_OFF) WiFi.disconnect(true);

  WiFi.mode(WIFI_STA);
  // Modem sleep between beacons. On a node that talks for two seconds every
  // five minutes this is most of the WiFi power budget.
  WiFi.setSleep(true);
  applyTxPower(dbm);
  WiFi.begin(netcfgSsid(), netcfgPassword());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < ASSOCIATE_TIMEOUT_MS)
    delay(250);

  if (easeOff) {
    Serial.flush();
    setCpuFrequencyMhz(240);
    radioStandUp();
    uiResumePanel();
  }

  if (WiFi.status() != WL_CONNECTED) {
    s_associateFailures++;
    LOG_W(TAG_NET, E_WIFI_FAIL, s_associateFailures);

    // Backing off matters more than it looks: a node retrying every second on
    // a network that is not there spends more power than one that reports.
    s_backoffMs = min(s_backoffMs * 2u, BACKOFF_MAX_MS);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  s_associateFailures = 0;
  s_backoffMs = BACKOFF_MIN_MS;
  LOG_I(TAG_NET, E_WIFI_UP, (uint32_t)WiFi.localIP());
  return true;
}

static void ensureTime() {
  if (s_timeSynced) return;

  // UTC with no daylight offset. A device log that shifts by an hour twice a
  // year is a device log nobody can line up with anything.
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  const uint32_t started = millis();
  while (millis() - started < 8000) {
    const time_t now = time(nullptr);
    if (now > SANE_EPOCH) {
      s_timeSynced = true;
      LOG_I(TAG_NET, E_TIME_SYNCED, (uint32_t)now);
      return;
    }
    delay(200);
  }
}

// --- log upload -----------------------------------------------------------

static void uploadLogs() {
  const uint32_t pushed = logPushed();
  if (pushed <= s_logCursor) return;

  const uint16_t held = logCount();
  uint32_t unsent = pushed - s_logCursor;

  if (unsent > held) {
    // The ring wrapped while the link was down. The lost records are gone;
    // saying so is the only honest option, and a gap counter is what makes a
    // silent hole in a dump explainable months later.
    s_logGap += unsent - held;
    s_logCursor = pushed - held;
    unsent = held;
  }

  const uint16_t batch = (uint16_t)min<uint32_t>(unsent, LOG_BATCH_MAX);

  // Oldest first. logPeek() counts from the newest, so the oldest unsent
  // record is at index unsent-1. Sending in this order means a batch cut short
  // by a lost link leaves one contiguous gap at the end rather than holes
  // scattered through the middle.
  static log_rec_t records[LOG_BATCH_MAX];
  uint16_t collected = 0;
  for (uint16_t i = 0; i < batch; i++) {
    const uint16_t index = (uint16_t)(unsent - 1u - i);
    if (logPeek(index, records[collected])) collected++;
  }

  if (collected == 0) return;

  const int status = fleetPostLogs(records, collected);
  if (status == 202 || status == 200) s_logCursor += collected;
}

// --- one cycle ------------------------------------------------------------

static void checkForUpdate(bool apply) {
  ota_manifest_t manifest;
  const int status = fleetGetTargetFirmware(manifest);

  if (status == 204) { // nothing to do, and the cheapest answer to handle
    s_updateAvailable = false;
    return;
  }
  if (status != 200) return;

  s_updateAvailable = true;
  strncpy(s_targetVersion, manifest.version, sizeof(s_targetVersion) - 1);
  LOG_I(TAG_OTA, E_OTA_AVAILABLE, manifest.size_bytes);

  const ota_gate_t gate = otaCheckGates(manifest);
  if (gate != OTA_GATE_OK) {
    LOG_W(TAG_OTA, E_OTA_REFUSED, (uint32_t)gate);
    if (gate == OTA_GATE_BLOCKED_VERSION) LOG_W(TAG_OTA, E_OTA_BLOCKED, 0);
    return;
  }

  if (!apply) return;

  // Returns only on failure: on success the node is already rebooting into
  // the new image, on trial, with the rollback net under it.
  otaApply(manifest);
}

static void runCycle() {
  // Consumed up front and unconditionally. A request that stayed set after a
  // failed attempt would be re-read by the wait loop below, which would break
  // out of its delay immediately and spin - an operator typing `ota update` on
  // a node with no link would peg a core until the link came back.
  const bool askedToCheck = s_updateCheckRequested;
  const bool askedToApply = s_applyRequested;
  s_updateCheckRequested = false;
  s_applyRequested = false;

  fleet_checkin_t checkIn;
  const int status = fleetPostHealth(checkIn);
  const bool ok = (status == 202 || status == 200);

  if (ok) {
    s_lastCheckInMs = millis();
    s_updateAvailable = checkIn.update_available;
    strncpy(s_targetVersion, checkIn.target_version, sizeof(s_targetVersion) - 1);
    uploadLogs();
  }

  // The evidence an image on trial has been waiting for: it boots, it passes
  // its own self-test, and something outside it can still reach it. Called
  // whether or not the check-in worked, because the trial also has to be able
  // to run out.
  otaTick(ok);

  if (!ok) return;

  if (!askedToCheck && !s_updateAvailable) return;

  // Automatic when the service has assigned this node a different version and
  // ota_enabled is 1 - that switch is what an operator turns off on a node
  // they do not want touched. `ota update` forces it either way.
  const bool apply = askedToApply || (config().ota_enabled && s_updateAvailable);
  checkForUpdate(apply);
}

// --- the task -------------------------------------------------------------

static void netTask(void * /*arg*/) {
  // The supply gave way partway through the last boot. Whatever else happens,
  // do not be the first thing to draw current this time.
  if (brownoutStreak() > 0) vTaskDelay(pdMS_TO_TICKS(BROWNOUT_SETTLE_MS));

  for (;;) {
    // Checked before wifi_enabled and before everything else, because this is
    // the one condition where bringing the radio up is what stops the node
    // running at all. Safe mode does not cover it: safe mode leaves the uplink
    // on deliberately, so that a node which keeps crashing can still be cured
    // remotely - and if the uplink is itself the cause, that is a reset loop
    // with no way out. This is the way out.
    if (netBrownoutHold()) {
      if (!s_warnedBrownout) {
        LOG_E(TAG_NET, E_NET_BROWNOUT_HOLD, brownoutStreak());
        uiPostBanner("WIFI OFF: BROWNOUT");
        s_warnedBrownout = true;
      }
      if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (!config().wifi_enabled) {
      if (!s_warnedDisabled) {
        LOG_I(TAG_NET, E_NET_DISABLED, 0);
        s_warnedDisabled = true;
      }
      if (netIsConnected()) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    s_warnedDisabled = false;

    if (!netcfgIsProvisioned()) {
      // Said once, not every five minutes. A node nobody has commissioned yet
      // should not spend its ring log repeating that fact.
      if (!s_warnedUnprovisioned) {
        LOG_W(TAG_NET, E_NET_UNPROVISIONED, 0);
        s_warnedUnprovisioned = true;
      }
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }
    s_warnedUnprovisioned = false;

    // An empty key is not "unprovisioned" - netcfgIsProvisioned() deliberately
    // does not require one, because a service that wants no key is a legitimate
    // thing to point a node at. But the fleet service does want one, and the
    // symptom of never having set it is a 401 on every report forever, which
    // reads like a broken deployment rather than a missing command. Say it
    // once, plainly, before the first attempt.
    if (netcfgApiKey()[0] == '\0' && !s_warnedNoKey) {
      LOG_W(TAG_NET, E_NET_NO_API_KEY, 0);
      s_warnedNoKey = true;
    }

    if (ensureAssociated()) {
      ensureTime();
      runCycle();
    } else {
      // An image on trial still has to be able to time out while the network
      // is down, or a node that came up on a bad image and cannot associate
      // would sit on it forever.
      otaTick(false);
    }

    uiRefresh();

    // Sliced into one-second waits so `net report` and `ota update` do not sit
    // behind a five-minute vTaskDelay.
    // The delay comes first in every iteration, so no combination of pending
    // flags can turn this into a busy loop.
    const uint32_t periodS = config().report_period_s;
    for (uint32_t elapsed = 0; elapsed < periodS; elapsed++) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (s_reportRequested || s_updateCheckRequested) {
        s_reportRequested = false;
        break;
      }
    }
  }
}

// --- public ---------------------------------------------------------------

bool netBegin() {
  netcfgBegin();

  // Nothing is associated here. Bringing WiFi up during setup() would add
  // seconds to a boot whose whole point is to reach the POST and the splash
  // quickly, and a node with no access point in range would spend them for
  // nothing.
  WiFi.persistent(false); // credentials live in our NVS namespace, not the SDK's
  WiFi.mode(WIFI_OFF);
  return netcfgIsProvisioned();
}

bool netTaskStart(UBaseType_t priority, BaseType_t core) {
  // 10 kB because a TLS handshake, the HTTP client, JSON parsing and an OTA
  // download all run on this stack. The default 4 kB overflows during the
  // handshake, and a stack overflow in a task nobody is watching looks exactly
  // like a random reboot.
  return xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, priority, nullptr,
                                 core) == pdPASS;
}

bool netIsConnected() { return WiFi.status() == WL_CONNECTED; }
bool netBrownoutHold() { return brownoutStreak() >= BROWNOUT_HOLD_AT; }
bool netTimeIsSynced() { return s_timeSynced; }
uint32_t netLastCheckInMs() { return s_lastCheckInMs; }
const char *netTargetVersion() { return s_targetVersion; }
bool netUpdateAvailable() { return s_updateAvailable; }

void netRequestReport() { s_reportRequested = true; }

void netRequestUpdateCheck(bool apply) {
  s_applyRequested = apply;
  s_updateCheckRequested = true;
}

void netPrintStatus(Print &out) {
  if (netBrownoutHold()) {
    out.printf("wifi      HELD OFF - %u brownout resets since power was applied\n",
               brownoutStreak());
    out.println("          The supply cannot deliver what the WiFi radio draws when");
    out.println("          it powers up. That burst comes before any transmission,");
    out.println("          so no power setting avoids it - this is the cable, the");
    out.println("          port, or the missing battery.");
    out.println("          Fit the battery, or use a short cable on a powered port,");
    out.println("          then POWER-CYCLE. A reboot deliberately does not clear");
    out.println("          this: only somebody at the hardware can change the answer.");
    return;
  }

  out.printf("wifi      %s\n", config().wifi_enabled ? "enabled" : "disabled by config");
  out.printf("tx power  %u dBm configured", config().wifi_tx_dbm);
  if (s_appliedDbm) out.printf(", %d dBm applied", s_appliedDbm);
  if (brownoutStreak())
    out.printf("   (held down: %u brownout%s since power-on)", brownoutStreak(),
               brownoutStreak() == 1 ? "" : "s");
  out.println();
  out.printf("ssid      %s\n", netcfgSsid()[0] ? netcfgSsid() : "(not set)");

  if (netIsConnected()) {
    out.printf("link      up  ip %s  rssi %d dBm\n", WiFi.localIP().toString().c_str(),
               WiFi.RSSI());
  } else {
    out.printf("link      down  (%u consecutive failures, retrying in up to %lu s)\n",
               s_associateFailures, (unsigned long)(s_backoffMs / 1000u));
  }

  out.printf("clock     %s\n", s_timeSynced ? "synced (UTC)" : "not synced - HTTPS will refuse");
  out.printf("service   %s\n", netcfgBaseUrl()[0] ? netcfgBaseUrl() : "(not set)");
  if (netcfgApiKey()[0]) {
    out.println("api key   set");
  } else {
    out.println("api key   NOT SET  <- the service refuses every report with");
    out.println("                     401 until `net set key <key>` is run");
  }
  out.printf("period    %u s\n", config().report_period_s);

  if (s_lastCheckInMs)
    out.printf("last ok   %lu s ago\n", (unsigned long)((millis() - s_lastCheckInMs) / 1000u));
  else
    out.println("last ok   never");

  out.printf("logs      %lu sent, %lu lost to ring wrap\n", (unsigned long)s_logCursor,
             (unsigned long)s_logGap);

  if (s_targetVersion[0])
    out.printf("target    %s%s\n", s_targetVersion,
               s_updateAvailable ? "  <- UPDATE AVAILABLE" : "  (current)");
  else
    out.println("target    not assigned");
}
