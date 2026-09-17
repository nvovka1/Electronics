#include "settings.h"

#include <Preferences.h>

#include "config.h"
#include "log.h"

Settings settings;

namespace {

// Two namespaces, because they answer different questions and are reset
// separately: `node` is what this board is, `net` is where it reports. A
// `reset` of one must not wipe the other.
constexpr const char *NodeNamespace = "node";
constexpr const char *NetNamespace = "net";
constexpr const char *ReplayNamespace = "replay";

Preferences nodePrefs;
Preferences netPrefs;

void copyInto(char *destination, size_t capacity, const char *source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

// A node that has never been provisioned still needs a serial, and it has to be
// stable across reboots - so it comes from the chip MAC rather than a counter
// or a random value.
void deriveSerial(char *out, size_t capacity) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, capacity, "%s%04X", SerialPrefix, (unsigned)(mac & 0xFFFFu));
}

} // namespace

void settingsLoad() {
  nodePrefs.begin(NodeNamespace, false);
  netPrefs.begin(NetNamespace, false);

  settings.nodeId = nodePrefs.getUShort("node_id", DefaultNodeId);
  settings.autoArmSeconds = nodePrefs.getULong("autoarm", DefaultAutoArmSeconds);

  String storedSerial = nodePrefs.getString("serial", "");
  if (storedSerial.length() > 0) {
    copyInto(settings.serial, sizeof(settings.serial), storedSerial.c_str());
  } else {
    deriveSerial(settings.serial, sizeof(settings.serial));
  }

  // The build-time values are defaults, not settings: anything stored wins, and
  // nothing here is ever written back to NVS.
  copyInto(settings.ssid, sizeof(settings.ssid),
           netPrefs.getString("ssid", NET_DEFAULT_SSID).c_str());
  copyInto(settings.password, sizeof(settings.password),
           netPrefs.getString("pass", NET_DEFAULT_PASS).c_str());
  copyInto(settings.baseUrl, sizeof(settings.baseUrl),
           netPrefs.getString("url", NET_DEFAULT_URL).c_str());
  copyInto(settings.apiKey, sizeof(settings.apiKey),
           netPrefs.getString("key", NET_DEFAULT_KEY).c_str());

  // Some proxies redirect a doubled slash and others refuse it, so a trailing
  // one is removed here rather than at every call site.
  size_t urlLength = strlen(settings.baseUrl);
  while (urlLength > 0 && settings.baseUrl[urlLength - 1] == '/') {
    settings.baseUrl[--urlLength] = '\0';
  }

  settings.bootCount = nodePrefs.getULong("boots", 0) + 1;
  nodePrefs.putULong("boots", settings.bootCount);

  LOG_INFO(TagCfg, CodeCfgLoaded, (int32_t)settings.nodeId);
  LOG_INFO(TagSys, CodeBootCount, (int32_t)settings.bootCount);
}

bool settingsSaveNodeId(uint16_t nodeId) {
  settings.nodeId = nodeId;
  const bool ok = nodePrefs.putUShort("node_id", nodeId) > 0;
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail, nodeId);
  return ok;
}

bool settingsSaveAutoArmSeconds(uint32_t seconds) {
  settings.autoArmSeconds = seconds;
  const bool ok = nodePrefs.putULong("autoarm", seconds) > 0;
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail,
         (int32_t)seconds);
  return ok;
}

bool settingsSaveWifi(const char *ssid, const char *password) {
  copyInto(settings.ssid, sizeof(settings.ssid), ssid);
  copyInto(settings.password, sizeof(settings.password), password);

  const bool ok = netPrefs.putString("ssid", settings.ssid) > 0 &&
                  netPrefs.putString("pass", settings.password) > 0;
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail, 0);
  return ok;
}

bool settingsSaveBaseUrl(const char *url) {
  copyInto(settings.baseUrl, sizeof(settings.baseUrl), url);
  const bool ok = netPrefs.putString("url", settings.baseUrl) > 0;
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail, 0);
  return ok;
}

bool settingsSaveApiKey(const char *key) {
  copyInto(settings.apiKey, sizeof(settings.apiKey), key);
  const bool ok = netPrefs.putString("key", settings.apiKey) > 0;
  // The key itself is never logged, only that one was set and how long it was.
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail,
         (int32_t)strlen(settings.apiKey));
  return ok;
}

bool settingsReset() {
  nodePrefs.remove("node_id");
  nodePrefs.remove("autoarm");
  nodePrefs.remove("serial");
  netPrefs.clear();

  LOG_WARN(TagCfg, CodeCfgDefaults, 0);
  settingsLoad();
  return true;
}

uint32_t settingsLoadReplayCounter(uint16_t src) {
  Preferences replayPrefs;
  if (!replayPrefs.begin(ReplayNamespace, true)) return 0;

  char key[8];
  snprintf(key, sizeof(key), "%u", (unsigned)src);
  const uint32_t counter = replayPrefs.getULong(key, 0);

  replayPrefs.end();
  return counter;
}

void settingsSaveReplayCounter(uint16_t src, uint32_t counter) {
  Preferences replayPrefs;
  if (!replayPrefs.begin(ReplayNamespace, false)) return;

  char key[8];
  snprintf(key, sizeof(key), "%u", (unsigned)src);
  replayPrefs.putULong(key, counter);

  replayPrefs.end();
}
