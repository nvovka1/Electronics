#include "core/calib.h"

#include <Preferences.h>

#include "core/log.h"

static constexpr const char *NS = "calib";
static constexpr const char *KEY_SERIAL = "serial";
static constexpr const char *KEY_VBAT_SCALE = "vbat_scale";

static char s_serial[16] = "LQ-000000";
static uint16_t s_vbatScaleQ10 = 1024; // x1.000: the nominal divider, uncorrected
static bool s_provisioned = false;

// A node that was never through the conveyor still needs a stable name, and
// the chip MAC is the only unique number it is born with. Two bytes give
// 65536 names, which is plenty to tell apart the boards on one desk, and the
// factory overwrites it with the real serial anyway.
static void deriveSerialFromMac() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(s_serial, sizeof(s_serial), "LQ-%02X%02X", mac[4], mac[5]);
}

bool calibBegin() {
  deriveSerialFromMac();

  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/true)) {
    // An absent namespace is the normal state of a board that has never been
    // provisioned, not a fault.
    return true;
  }

  char stored[sizeof(s_serial)] = {0};
  const size_t n = prefs.getString(KEY_SERIAL, stored, sizeof(stored));
  if (n > 0 && stored[0] != '\0') {
    strlcpy(s_serial, stored, sizeof(s_serial));
    s_provisioned = true;
  }

  s_vbatScaleQ10 = prefs.getUShort(KEY_VBAT_SCALE, s_vbatScaleQ10);
  prefs.end();

  // A scale far from unity means a corrupt or mis-entered value, and using it
  // would make every battery reading and every low-power refusal wrong.
  if (s_vbatScaleQ10 < 512 || s_vbatScaleQ10 > 2048) {
    LOG_W(TAG_BATT, E_BATT_UNTRUSTED, s_vbatScaleQ10);
    s_vbatScaleQ10 = 1024;
  }

  return true;
}

const char *calibSerial() { return s_serial; }

uint16_t calibVbatScaleQ10() { return s_vbatScaleQ10; }

bool calibIsProvisioned() { return s_provisioned; }

#if BUILD_PROVISIONING
bool calibSetSerial(const char *serial) {
  if (!serial || !serial[0] || strlen(serial) >= sizeof(s_serial)) return false;

  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return false;
  const bool ok = prefs.putString(KEY_SERIAL, serial) > 0;
  prefs.end();

  if (ok) {
    strlcpy(s_serial, serial, sizeof(s_serial));
    s_provisioned = true;
  }
  return ok;
}

bool calibSetVbatScale(uint16_t scaleQ10) {
  if (scaleQ10 < 512 || scaleQ10 > 2048) return false;

  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return false;
  const bool ok = prefs.putUShort(KEY_VBAT_SCALE, scaleQ10) > 0;
  prefs.end();

  if (ok) s_vbatScaleQ10 = scaleQ10;
  return ok;
}
#endif
