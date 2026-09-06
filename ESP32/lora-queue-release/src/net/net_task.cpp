#include "net/net_task.h"

#include <WiFi.h>
#include <time.h>

#include "core/config.h"
#include "core/log.h"
#include "core/netcfg.h"
#include "net/fleet_client.h"
#include "net/ota.h"
#include "tasks/ui_task.h"

// Records per POST. Thirty-two twelve-byte records make a body of about two
// kilobytes, which fits comfortably in one TLS session without a large heap
// allocation. The rest go in the next cycle: the ring holds 256, so nothing is
// lost unless the link has been down long enough to overwrite them - and when
// that happens the gap is logged rather than quietly closed.
static constexpr uint16_t LOG_BATCH_MAX = 32;

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

// --- association ----------------------------------------------------------

static bool ensureAssociated() {
  if (WiFi.status() == WL_CONNECTED) return true;

  const uint32_t now = millis();
  if (s_lastAttemptMs != 0 && now - s_lastAttemptMs < s_backoffMs) return false;
  s_lastAttemptMs = now;

  LOG_I(TAG_NET, E_WIFI_CONNECTING, s_associateFailures + 1u);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  // Modem sleep between beacons. On a node that talks for two seconds every
  // five minutes this is most of the WiFi power budget.
  WiFi.setSleep(true);
  WiFi.begin(netcfgSsid(), netcfgPassword());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < ASSOCIATE_TIMEOUT_MS)
    delay(250);

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
  for (;;) {
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
  out.printf("wifi      %s\n", config().wifi_enabled ? "enabled" : "disabled by config");
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
  out.printf("api key   %s\n", netcfgApiKey()[0] ? "set" : "(not set)");
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
