#include "settings.h"

#include <Preferences.h>

#include "config.h"
#include "log.h"

Settings settings;

static const char *Namespace = "mavtel";
static Preferences _preferences;

static void copyInto(char *destination, size_t size, const char *source) {
  strncpy(destination, source, size - 1);
  destination[size - 1] = 0;
}

// A board nobody has named is named after itself. The last three bytes of the
// chip MAC are unique enough for a hangar and stable across reflashes, which
// are the only two properties that matter.
static void deriveSerial(char *out, size_t size) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, size, "uav-%02x%02x%02x", (unsigned)((mac >> 24) & 0xff),
           (unsigned)((mac >> 32) & 0xff), (unsigned)((mac >> 40) & 0xff));
}

// The preferences object must already be open.
static String readString(const char *key, const char *fallback) {
  return _preferences.isKey(key) ? _preferences.getString(key) : String(fallback);
}

// Everything except the boot counter. Split out because a reset re-reads the
// settings but is not itself a boot, and bumping the counter there would give
// two different flights the same number.
static void loadFields() {
  _preferences.begin(Namespace, false);

  char derived[24];
  deriveSerial(derived, sizeof(derived));

  // isKey first, rather than leaning on getString's default. Preferences logs
  // at ERROR when a key is absent even when a default was supplied, so a
  // factory-fresh board greets you with a screen of red about settings that are
  // working exactly as intended.
  const String storedSerial = readString("serial", derived);
  const String storedSsid = readString("ssid", NET_DEFAULT_SSID);
  const String storedPassword = readString("pass", NET_DEFAULT_PASS);
  const String storedUrl = readString("url", NET_DEFAULT_URL);
  const String storedKey = readString("key", NET_DEFAULT_KEY);

  copyInto(settings.serial, sizeof(settings.serial), storedSerial.c_str());
  copyInto(settings.ssid, sizeof(settings.ssid), storedSsid.c_str());
  copyInto(settings.password, sizeof(settings.password), storedPassword.c_str());
  copyInto(settings.baseUrl, sizeof(settings.baseUrl), storedUrl.c_str());
  copyInto(settings.apiKey, sizeof(settings.apiKey), storedKey.c_str());

  // A trailing slash doubles into the path and some proxies redirect that while
  // others refuse it, so it is removed once here rather than guarded at every
  // call site.
  size_t urlLength = strlen(settings.baseUrl);
  while (urlLength > 0 && settings.baseUrl[urlLength - 1] == '/') {
    settings.baseUrl[--urlLength] = 0;
  }

  settings.mavBaud = _preferences.getULong("baud", MavBaudDefault);
  settings.logRateHz = _preferences.getULong("rate", LogRateHzDefault);
  if (settings.logRateHz == 0 || settings.logRateHz > LogRateHzMax) {
    settings.logRateHz = LogRateHzDefault;
  }
  settings.uploadEnabled = _preferences.getBool("upload", true);
  settings.bootCount = _preferences.getULong("boots", 0);

  _preferences.end();

  LOG_INFO("cfg", "serial=%s boot=%lu baud=%lu rate=%luHz upload=%d", settings.serial,
           (unsigned long)settings.bootCount, (unsigned long)settings.mavBaud,
           (unsigned long)settings.logRateHz, settings.uploadEnabled ? 1 : 0);
}

void settingsLoad() {
  loadFields();

  _preferences.begin(Namespace, false);
  settings.bootCount++;
  _preferences.putULong("boots", settings.bootCount);
  _preferences.end();

  LOG_INFO("cfg", "boot=%lu", (unsigned long)settings.bootCount);
}

static bool putString(const char *key, const char *value, char *field, size_t size) {
  _preferences.begin(Namespace, false);
  const bool stored = _preferences.putString(key, value) > 0;
  _preferences.end();
  if (stored) copyInto(field, size, value);
  return stored;
}

bool settingsSaveSerial(const char *serial) {
  if (serial == nullptr || strlen(serial) == 0 || strlen(serial) > 23) return false;

  // The serial reaches a URL and a database key on the service side, which
  // checks it too. Checking here as well means a bad value is rejected while
  // somebody is watching the shell, rather than becoming 400s from the field.
  for (const char *c = serial; *c != 0; c++) {
    const bool allowed = isalnum((unsigned char)*c) || *c == '-' || *c == '_';
    if (!allowed) return false;
  }

  return putString("serial", serial, settings.serial, sizeof(settings.serial));
}

bool settingsSaveWifi(const char *ssid, const char *password) {
  if (ssid == nullptr || password == nullptr) return false;
  if (strlen(ssid) > 32 || strlen(password) > 64) return false;

  const bool savedSsid = putString("ssid", ssid, settings.ssid, sizeof(settings.ssid));
  const bool savedPassword =
      putString("pass", password, settings.password, sizeof(settings.password));
  return savedSsid && savedPassword;
}

bool settingsSaveBaseUrl(const char *url) {
  if (url == nullptr || strlen(url) > 127) return false;
  return putString("url", url, settings.baseUrl, sizeof(settings.baseUrl));
}

bool settingsSaveApiKey(const char *key) {
  if (key == nullptr || strlen(key) > 79) return false;
  return putString("key", key, settings.apiKey, sizeof(settings.apiKey));
}

bool settingsSaveMavBaud(uint32_t baud) {
  if (baud < 9600 || baud > 921600) return false;
  _preferences.begin(Namespace, false);
  const bool stored = _preferences.putULong("baud", baud) > 0;
  _preferences.end();
  if (stored) settings.mavBaud = baud;
  return stored;
}

bool settingsSaveLogRate(uint32_t hertz) {
  if (hertz == 0 || hertz > LogRateHzMax) return false;
  _preferences.begin(Namespace, false);
  const bool stored = _preferences.putULong("rate", hertz) > 0;
  _preferences.end();
  if (stored) settings.logRateHz = hertz;
  return stored;
}

bool settingsSaveUploadEnabled(bool enabled) {
  _preferences.begin(Namespace, false);
  const bool stored = _preferences.putBool("upload", enabled) > 0;
  _preferences.end();
  if (stored) settings.uploadEnabled = enabled;
  return stored;
}

bool settingsReset() {
  const uint32_t boots = settings.bootCount;

  _preferences.begin(Namespace, false);
  const bool cleared = _preferences.clear();
  _preferences.putULong("boots", boots);
  _preferences.end();

  if (cleared) loadFields();
  return cleared;
}
