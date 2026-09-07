#include "net/ota.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "core/config.h"
#include "core/log.h"
#include "net/netcfg.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "net/net_task.h"
#include "net/root_ca.h"
#include "tasks/ui_task.h"

static constexpr const char *NS = "ota";
static constexpr const char *KEY_BLOCKED = "blocked";
static constexpr const char *KEY_LAST_CRITICAL = "critmask";
static constexpr const char *KEY_EXPECT = "expect";

// Ten minutes to prove itself. Long enough to cover a WiFi access point that
// is slow to come back and a hosting instance that has to wake from sleep;
// short enough that a node nobody can reach is not left on a bad image
// overnight.
static constexpr uint32_t TRIAL_WINDOW_MS = 10u * 60u * 1000u;

// A check-in inside the first few seconds proves less than it looks like: a
// crash on a timer, a heap that only runs out under load, a task that dies on
// its second pass would all still be ahead. One minute of life before the
// image is called good.
static constexpr uint32_t TRIAL_MIN_UPTIME_MS = 60u * 1000u;

static constexpr size_t CHUNK = 1024;
static constexpr uint32_t STREAM_STALL_MS = 20000;
static constexpr uint32_t HEAP_FLOOR = 40u * 1024u;

static ota_state_t s_state = OTA_IDLE;
static uint8_t s_percent = 0;
static bool s_onTrial = false;
static uint32_t s_trialStartedMs = 0;
static char s_blockedVersion[24] = {0};

// The critical POST bits that were already failing on the last image to earn
// its place here. A radio that died in the field must not make every future
// update look like a regression and roll itself back - the node would end up
// permanently un-updatable for a fault no image can fix.
static uint16_t s_lastGoodCritical = 0;

// The version the manifest claimed for the image we just installed, carried
// across the reboot so the new image can check whether the claim was true.
static char s_expectedVersion[24] = {0};

// --- the rollback hook ----------------------------------------------------

// Arduino's initArduino() confirms a PENDING_VERIFY image the moment it
// reaches setup(), which would cancel the rollback before the firmware has
// demonstrated anything at all. Overriding this weak symbol keeps the decision
// here, where there is evidence to base it on.
//
// The cost of getting this wrong is symmetric and worth stating: returning
// true and then never confirming means every reboot silently reverts the
// update. Every path out of the trial below therefore ends in a confirm or in
// a deliberate rollback - never in nothing.
extern "C" bool verifyRollbackLater() { return true; }

// --- helpers --------------------------------------------------------------

static void loadPersistedState() {
  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/true)) return;
  prefs.getString(KEY_BLOCKED, s_blockedVersion, sizeof(s_blockedVersion));
  prefs.getString(KEY_EXPECT, s_expectedVersion, sizeof(s_expectedVersion));
  s_lastGoodCritical = prefs.getUShort(KEY_LAST_CRITICAL, 0);
  prefs.end();
}

static void clearExpectedVersion() {
  if (s_expectedVersion[0] == '\0') return;
  Preferences prefs;
  if (prefs.begin(NS, /*readOnly=*/false)) {
    prefs.remove(KEY_EXPECT);
    prefs.end();
  }
  s_expectedVersion[0] = '\0';
}

static void storeBlockedVersion(const char *version) {
  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return;
  prefs.putString(KEY_BLOCKED, version);
  prefs.end();
  strncpy(s_blockedVersion, version, sizeof(s_blockedVersion) - 1);
}

static void clearBlockedVersion() {
  if (s_blockedVersion[0] == '\0') return;
  Preferences prefs;
  if (prefs.begin(NS, /*readOnly=*/false)) {
    prefs.remove(KEY_BLOCKED);
    prefs.end();
  }
  s_blockedVersion[0] = '\0';
}

static bool hexEqualsIgnoringCase(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
  }
  return *a == '\0' && *b == '\0';
}

// --- boot classification --------------------------------------------------

void otaBegin() {
  loadPersistedState();

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    // A cable-flashed image, or one that has already earned its place. If this
    // node is running the version that was blocked, it is the rollback target
    // and the block has done its job - but the block names the version that
    // FAILED, so it only clears when a different version is confirmed.
    return;
  }

  s_onTrial = true;
  s_state = OTA_ON_TRIAL;
  s_trialStartedMs = millis();

  // Did we get the image the manifest promised?
  //
  // A manifest describes an image; nothing forces the two to agree, and a
  // relabelled one is an easy mistake to make by hand. The consequence is
  // nastier than it sounds: the service assigns 1.1.3, the node installs a
  // binary that reports 1.1.2, the service still wants 1.1.3, and the node
  // downloads a megabyte and reboots for the rest of its life.
  //
  // The image itself is fine - it is a real, verified build - so it is kept.
  // What gets blocked is the VERSION THE MANIFEST CLAIMED, which stops the
  // loop at exactly one iteration.
  if (s_expectedVersion[0] != '\0' &&
      !hexEqualsIgnoringCase(s_expectedVersion, FW_SEMVER)) {
    LOG_E(TAG_OTA, E_OTA_VERSION_MISMATCH, 0);
    storeBlockedVersion(s_expectedVersion);
    clearExpectedVersion();
    uiPostBanner("manifest version wrong");
  }

  // WARN, not INFO: a node in this state for longer than the trial window is
  // about to revert, and that is worth seeing in a dump.
  LOG_W(TAG_OTA, E_OTA_TRIAL, 0);
}

bool otaIsOnTrial() { return s_onTrial; }

uint32_t otaTrialSecondsLeft() {
  if (!s_onTrial) return 0;
  const uint32_t elapsed = millis() - s_trialStartedMs;
  if (elapsed >= TRIAL_WINDOW_MS) return 0;
  return (TRIAL_WINDOW_MS - elapsed) / 1000u;
}

// --- confirm and roll back ------------------------------------------------

bool otaConfirmNow() {
  if (!s_onTrial) return false;

  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return false;

  s_onTrial = false;
  s_state = OTA_IDLE;

  // This version boots, self-tests and reports. Whatever failed before, it was
  // not this one, so an old block must not keep refusing it forever.
  if (!hexEqualsIgnoringCase(s_blockedVersion, FW_SEMVER)) clearBlockedVersion();
  clearExpectedVersion();

  // Whatever is failing its POST right now is failing on an image that has
  // just proved itself, so it is the hardware, not the software. Recorded as
  // the baseline the next update is judged against.
  s_lastGoodCritical = postCriticalMask();
  Preferences prefs;
  if (prefs.begin(NS, /*readOnly=*/false)) {
    prefs.putUShort(KEY_LAST_CRITICAL, s_lastGoodCritical);
    prefs.end();
  }

  LOG_I(TAG_OTA, E_OTA_CONFIRMED, millis() / 1000u);
  uiPostBanner("update confirmed");
  return true;
}

bool otaRollbackNow(ota_rollback_reason_t reason) {
  LOG_E(TAG_OTA, E_OTA_ROLLBACK, (uint32_t)reason);

  // Recorded before the reboot, because after it this code is not running any
  // more. Without this the service offers the same broken version again on the
  // next check-in and the node spends its battery failing in a circle.
  storeBlockedVersion(FW_SEMVER);

  uiPostBanner("ROLLING BACK");
  delay(400); // let the screen and the UART actually show it

  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();

  // Only reached when there is nothing to roll back to. A suspect image that
  // runs beats a device that will not boot, so the image is confirmed instead
  // and the failure is left loudly in the log.
  LOG_E(TAG_OTA, E_OTA_ROLLBACK, (uint32_t)err);
  if (s_onTrial) {
    esp_ota_mark_app_valid_cancel_rollback();
    s_onTrial = false;
    s_state = OTA_IDLE;
  }
  return false;
}

void otaTick(bool checkedIn) {
  if (!s_onTrial) return;

  // A critical block that was NOT already failing before the update. Bits that
  // were failing beforehand are a broken radio or a broken supply; going back
  // to the previous image does not mend either, and doing so would block this
  // version and leave the node stuck on an older one for a fault no image can
  // fix. Only a new failure is treated as a regression.
  const uint16_t regressions = (uint16_t)(postCriticalMask() & ~s_lastGoodCritical);
  if (regressions != 0) {
    otaRollbackNow(OTA_ROLLBACK_POST_FAILED);
    return;
  }

  const uint32_t elapsed = millis() - s_trialStartedMs;

  if (checkedIn && elapsed >= TRIAL_MIN_UPTIME_MS) {
    otaConfirmNow();
    return;
  }

  if (elapsed >= TRIAL_WINDOW_MS) {
    // It boots and it self-tests, but nothing can reach it. That is precisely
    // the state from which a node can never be repaired remotely.
    otaRollbackNow(OTA_ROLLBACK_TRIAL_TIMEOUT);
  }
}

// --- preconditions --------------------------------------------------------

ota_gate_t otaCheckGates(const ota_manifest_t &manifest) {
  if (manifest.version[0] == '\0' || manifest.size_bytes == 0)
    return OTA_GATE_NO_MANIFEST;

  if (!config().ota_enabled) return OTA_GATE_DISABLED;

  // Safe mode is deliberately NOT a refusal. A node that has crashed three
  // times running is exactly the node a new image is meant to cure, and in
  // safe mode the tasks that were crashing are switched off, so it is the
  // steadiest this device gets. The download itself risks nothing: until the
  // boot slot is switched, an interrupted update is indistinguishable from no
  // update at all.
  if (s_onTrial) return OTA_GATE_ON_TRIAL;

  if (s_blockedVersion[0] != '\0' &&
      hexEqualsIgnoringCase(s_blockedVersion, manifest.version))
    return OTA_GATE_BLOCKED_VERSION;

  // An image built for a different board revision has different pin
  // assignments. It will flash, boot, and drive whatever happens to be on the
  // pin it thinks is the display reset - which on this PICO-D4 board is the
  // embedded flash chip select, and that is a brick.
  if (strcmp(manifest.hardware_id, FW_HW_ID) != 0) return OTA_GATE_WRONG_HARDWARE;

  if (strcmp(manifest.version, FW_SEMVER) == 0) return OTA_GATE_SAME_VERSION;

  // Unlike a config write, an untrusted reading is a refusal here. A config
  // write is a few hundred bytes and is retried at leisure; this is a third of
  // a megabyte of flash followed by a reboot into code that has never run, and
  // it is not a thing to do on a battery whose level is a guess.
  //
  // Both gates are skipped on a node declared permanently powered, because
  // neither has anything to protect against: there is no charge to run out
  // halfway through. Note that an interrupted update is safe here for the same
  // reason it is safe everywhere else in this file - nothing switches slots
  // until every byte is verified, and a supply lost after that leaves the new
  // image on trial, which reverts on its own if it never confirms.
  if (config().has_battery) {
    if (!batteryTrusted()) return OTA_GATE_BATTERY_UNTRUSTED;
    if (batteryMillivolts() < config().ota_vbat_min_mv) return OTA_GATE_LOW_BATTERY;
  }

  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (!target || manifest.size_bytes > target->size) return OTA_GATE_TOO_BIG;

  // TLS buffers, the HTTP client and the flash write path all want heap at the
  // same time. Finding out halfway through is a half-written slot.
  if (ESP.getFreeHeap() < HEAP_FLOOR) return OTA_GATE_LOW_HEAP;

  return OTA_GATE_OK;
}

const char *otaGateText(ota_gate_t gate) {
  switch (gate) {
    case OTA_GATE_OK: return "ok";
    case OTA_GATE_DISABLED: return "ota_enabled is 0";
    case OTA_GATE_ON_TRIAL: return "this image is still on trial itself";
    case OTA_GATE_LOW_BATTERY: return "battery below ota_vbat_min_mv";
    case OTA_GATE_BATTERY_UNTRUSTED: return "the ADC self-test failed, so the battery reading means nothing";
    case OTA_GATE_WRONG_HARDWARE: return "the image is built for a different board";
    case OTA_GATE_SAME_VERSION: return "already running this version";
    case OTA_GATE_TOO_BIG: return "the image does not fit the inactive slot";
    case OTA_GATE_LOW_HEAP: return "not enough free heap to hold a TLS session and write flash";
    case OTA_GATE_BLOCKED_VERSION: return "this version already failed its trial on this node";
    case OTA_GATE_NO_MANIFEST: return "no usable manifest";
  }
  return "unknown";
}

// --- the download ---------------------------------------------------------

static void reportProgress(const char *version, uint32_t written, uint32_t total) {
  const uint8_t percent = total ? (uint8_t)((uint64_t)written * 100u / total) : 0;
  if (percent == s_percent) return;

  s_percent = percent;
  uiShowOtaProgress(version, percent);

  // Every ten per cent, not every chunk: at 1 kB a chunk a third of a megabyte
  // is 330 records, which is more than the whole ring holds.
  if (percent % 10 == 0) LOG_I(TAG_OTA, E_OTA_PROGRESS, percent);
}

bool otaApply(const ota_manifest_t &manifest) {
  const ota_gate_t gate = otaCheckGates(manifest);
  if (gate != OTA_GATE_OK) {
    LOG_W(TAG_OTA, E_OTA_REFUSED, (uint32_t)gate);
    s_state = OTA_FAILED;
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_W(TAG_OTA, E_OTA_REFUSED, (uint32_t)OTA_GATE_NO_MANIFEST);
    return false;
  }

  const bool https = strncmp(manifest.download_url, "https://", 8) == 0;
  WiFiClientSecure tls;
  WiFiClient plain;
  WiFiClient *client = &plain;

  if (https) {
    if (config().tls_verify) {
      if (!netTimeIsSynced()) {
        LOG_W(TAG_OTA, E_OTA_REFUSED, (uint32_t)OTA_GATE_NO_MANIFEST);
        return false;
      }
      tls.setCACert(FLEET_ROOT_CA_PEM);
    } else {
      tls.setInsecure();
      LOG_W(TAG_NET, E_TLS_INSECURE, 0);
    }
    client = &tls;
  }

  HTTPClient http;
  http.setConnectTimeout(12000);
  http.setTimeout(STREAM_STALL_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!http.begin(*client, manifest.download_url)) {
    LOG_E(TAG_OTA, E_OTA_DOWNLOAD_FAIL, 0);
    s_state = OTA_FAILED;
    return false;
  }
  http.addHeader("X-Api-Key", netcfgApiKey());

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG_E(TAG_OTA, E_OTA_DOWNLOAD_FAIL, (uint32_t)status);
    http.end();
    s_state = OTA_FAILED;
    return false;
  }

  // The server's own idea of the length has to agree with the manifest, or one
  // of the two is describing a different file.
  const int contentLength = http.getSize();
  if (contentLength > 0 && (uint32_t)contentLength != manifest.size_bytes) {
    LOG_E(TAG_OTA, E_OTA_DOWNLOAD_FAIL, (uint32_t)contentLength);
    http.end();
    s_state = OTA_FAILED;
    return false;
  }

  if (!Update.begin(manifest.size_bytes, U_FLASH)) {
    LOG_E(TAG_OTA, E_OTA_WRITE_FAIL, Update.getError());
    http.end();
    s_state = OTA_FAILED;
    return false;
  }

  LOG_I(TAG_OTA, E_OTA_BEGIN, manifest.size_bytes);
  s_state = OTA_DOWNLOADING;
  s_percent = 0;
  uiShowOtaProgress(manifest.version, 0);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, /*is224=*/0);

  WiFiClient *stream = http.getStreamPtr();

  // Static, not on the stack: this runs on the network task, which is already
  // carrying an mbedtls session, and only that task ever calls otaApply().
  static uint8_t buffer[CHUNK];
  uint32_t written = 0;
  uint32_t lastByteMs = millis();
  bool failed = false;

  while (written < manifest.size_bytes) {
    const size_t available = stream->available();

    if (available == 0) {
      if (!stream->connected() || millis() - lastByteMs > STREAM_STALL_MS) {
        LOG_E(TAG_OTA, E_OTA_DOWNLOAD_FAIL, written);
        failed = true;
        break;
      }
      delay(10);
      continue;
    }

    const size_t want = min(min(available, CHUNK),
                            (size_t)(manifest.size_bytes - written));
    const int got = stream->readBytes(buffer, want);
    if (got <= 0) {
      delay(10);
      continue;
    }

    if (Update.write(buffer, got) != (size_t)got) {
      LOG_E(TAG_OTA, E_OTA_WRITE_FAIL, Update.getError());
      failed = true;
      break;
    }

    // Hashed as it goes past. Buffering a third of a megabyte to hash it
    // afterwards is not an option on this part, and hashing what was written
    // rather than what will be read back is what catches a truncated or
    // tampered image before anything switches.
    mbedtls_sha256_update_ret(&sha, buffer, got);

    written += got;
    lastByteMs = millis();
    reportProgress(manifest.version, written, manifest.size_bytes);
  }

  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (failed || written != manifest.size_bytes) {
    Update.abort();
    s_state = OTA_FAILED;
    uiPostBanner("update failed");
    return false;
  }

  s_state = OTA_VERIFYING;

  char hex[65];
  for (size_t i = 0; i < sizeof(digest); i++) sprintf(&hex[i * 2], "%02x", digest[i]);
  hex[64] = '\0';

  if (!hexEqualsIgnoringCase(hex, manifest.sha256)) {
    // Every byte is already in the inactive slot, and that is fine: nothing
    // boots from it until the boot partition is switched, and abort() means it
    // never will be.
    Update.abort();
    LOG_E(TAG_OTA, E_OTA_HASH_MISMATCH, written);
    s_state = OTA_FAILED;
    uiPostBanner("bad image, ignored");
    return false;
  }

  // This is the line that switches the boot slot. Everything before it was
  // reversible by doing nothing at all.
  if (!Update.end(true)) {
    LOG_E(TAG_OTA, E_OTA_WRITE_FAIL, Update.getError());
    s_state = OTA_FAILED;
    return false;
  }

  s_state = OTA_STAGED;
  LOG_I(TAG_OTA, E_OTA_STAGED, written);

  // Recorded before the reboot, because after it this code is not running.
  // The image that comes up checks this against its own FW_SEMVER.
  {
    Preferences prefs;
    if (prefs.begin(NS, /*readOnly=*/false)) {
      prefs.putString(KEY_EXPECT, manifest.version);
      prefs.end();
    }
  }

  uiShowOtaProgress(manifest.version, 100);
  uiPostBanner("rebooting to update");
  delay(1200); // long enough to be read off the glass

  esp_restart();
  return true; // not reached
}

ota_state_t otaState() { return s_state; }
uint8_t otaProgressPercent() { return s_percent; }

void otaPrint(Print &out) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

  out.printf("running   %s  (%lu KB slot)\n", running ? running->label : "?",
             running ? (unsigned long)(running->size / 1024u) : 0ul);
  out.printf("target    %s  (%lu KB slot)\n", next ? next->label : "none",
             next ? (unsigned long)(next->size / 1024u) : 0ul);
  out.printf("enabled   %s\n", config().ota_enabled ? "yes" : "no");
  if (config().has_battery)
    out.printf("vbat gate %u mV  (now %u mV%s)\n", config().ota_vbat_min_mv,
               batteryMillivolts(), batteryTrusted() ? "" : ", untrusted");
  else
    out.println("vbat gate off  (has_battery 0: this node is permanently powered)");
  out.printf("free heap %lu bytes\n", (unsigned long)ESP.getFreeHeap());

  if (s_onTrial)
    out.printf("state     ON TRIAL, %lu s left before automatic rollback\n",
               (unsigned long)otaTrialSecondsLeft());
  else if (s_state == OTA_DOWNLOADING)
    out.printf("state     downloading, %u%%\n", s_percent);
  else
    out.println("state     idle");

  if (s_blockedVersion[0])
    out.printf("blocked   %s  (failed its trial on this node)\n", s_blockedVersion);
  if (s_expectedVersion[0])
    out.printf("expecting %s on the next boot\n", s_expectedVersion);
  if (s_lastGoodCritical)
    out.printf("baseline  critical POST 0x%04X was already failing before the "
               "last confirmed image\n",
               s_lastGoodCritical);
}
