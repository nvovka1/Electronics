#include "core/netcfg.h"

#include <Preferences.h>

#include "core/config.h"
#include "core/log.h"

static constexpr const char *NS = "net";

#ifndef NET_DEFAULT_SSID
#define NET_DEFAULT_SSID ""
#endif
#ifndef NET_DEFAULT_PASS
#define NET_DEFAULT_PASS ""
#endif
#ifndef NET_DEFAULT_URL
#define NET_DEFAULT_URL ""
#endif
#ifndef NET_DEFAULT_KEY
#define NET_DEFAULT_KEY ""
#endif

typedef struct {
  const char *name; // what an operator types
  const char *key;  // what NVS stores it under
  char *buffer;
  size_t max;
  const char *fallback;
  bool secret;
} netcfg_entry_t;

static char s_ssid[NET_SSID_MAX + 1];
static char s_pass[NET_PASS_MAX + 1];
static char s_url[NET_URL_MAX + 1];
static char s_key[NET_KEY_MAX + 1];

// The NVS key is short and fixed while the operator-facing name can be spelled
// for a human: NVS keys are capped at 15 characters and renaming one silently
// orphans whatever is already stored under the old one.
static netcfg_entry_t s_entries[NETCFG_FIELD_COUNT] = {
    {"ssid", "ssid", s_ssid, NET_SSID_MAX, NET_DEFAULT_SSID, false},
    {"pass", "pass", s_pass, NET_PASS_MAX, NET_DEFAULT_PASS, true},
    {"url", "url", s_url, NET_URL_MAX, NET_DEFAULT_URL, false},
    {"key", "key", s_key, NET_KEY_MAX, NET_DEFAULT_KEY, true},
};

static netcfg_entry_t *entryByName(const char *name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < NETCFG_FIELD_COUNT; i++)
    if (strcmp(s_entries[i].name, name) == 0) return &s_entries[i];
  return nullptr;
}

// Trailing slashes are stripped so the client can concatenate a path without
// ever producing "//api/v1/...", which some proxies redirect and some reject.
static void stripTrailingSlashes(char *url) {
  size_t n = strlen(url);
  while (n > 0 && url[n - 1] == '/') url[--n] = '\0';
}

void netcfgBegin() {
  Preferences prefs;
  const bool opened = prefs.begin(NS, /*readOnly=*/true);

  for (size_t i = 0; i < NETCFG_FIELD_COUNT; i++) {
    netcfg_entry_t &e = s_entries[i];
    e.buffer[0] = '\0';

    // getString writes the NUL itself and truncates rather than overrunning.
    if (opened) prefs.getString(e.key, e.buffer, e.max + 1);

    // Nothing stored: fall back to what the image was built with, and do NOT
    // write it back. A default that has been persisted is a default nobody can
    // tell apart from a decision somebody made.
    if (e.buffer[0] == '\0') strncpy(e.buffer, e.fallback, e.max);
    e.buffer[e.max] = '\0';
  }

  if (opened) prefs.end();

  stripTrailingSlashes(s_url);

  LOG_I(TAG_NET, E_NET_CFG_LOADED, (uint32_t)netcfgIsProvisioned());
}

const char *netcfgSsid() { return s_ssid; }
const char *netcfgPassword() { return s_pass; }
const char *netcfgBaseUrl() { return s_url; }
const char *netcfgApiKey() { return s_key; }

bool netcfgIsProvisioned() { return s_ssid[0] != '\0' && s_url[0] != '\0'; }

netcfg_set_result_t netcfgSet(const char *field, const char *value) {
  netcfg_entry_t *e = entryByName(field);
  if (!e) return NETCFG_SET_UNKNOWN_FIELD;
  if (!value) return NETCFG_SET_INVALID;

  if (strlen(value) > e->max) return NETCFG_SET_TOO_LONG;

  // A base URL that is not http(s) would be a silent no-op later, at the far
  // end of a five-minute timer, instead of an error the operator can see now.
  if (e == &s_entries[NETCFG_URL] && value[0] != '\0' &&
      strncmp(value, "http://", 7) != 0 && strncmp(value, "https://", 8) != 0)
    return NETCFG_SET_INVALID;

  // The same anti-brick rule the config write obeys: flash is the hungriest
  // thing this node does, and it is not worth doing on a cell this low.
  if (!configPowerAllowsWrite()) return NETCFG_SET_LOW_POWER;

  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return NETCFG_SET_WRITE_FAILED;
  const size_t written = prefs.putString(e->key, value);
  prefs.end();

  if (written != strlen(value)) return NETCFG_SET_WRITE_FAILED;

  strncpy(e->buffer, value, e->max);
  e->buffer[e->max] = '\0';
  if (e == &s_entries[NETCFG_URL]) stripTrailingSlashes(s_url);

  // The value itself is never logged: two of these four are secrets, and the
  // ring log leaves this device.
  LOG_I(TAG_NET, E_NET_CFG_CHANGED, (uint32_t)(e - s_entries));
  return NETCFG_SET_OK;
}

netcfg_set_result_t netcfgReset() {
  if (!configPowerAllowsWrite()) return NETCFG_SET_LOW_POWER;

  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return NETCFG_SET_WRITE_FAILED;
  const bool cleared = prefs.clear();
  prefs.end();
  if (!cleared) return NETCFG_SET_WRITE_FAILED;

  // Back to whatever the image was built with, which for a factory image is
  // the commissioning network.
  netcfgBegin();
  return NETCFG_SET_OK;
}

const char *netcfgSetResultText(netcfg_set_result_t r) {
  switch (r) {
    case NETCFG_SET_OK: return "ok";
    case NETCFG_SET_UNKNOWN_FIELD: return "unknown field";
    case NETCFG_SET_TOO_LONG: return "too long";
    case NETCFG_SET_INVALID: return "invalid value";
    case NETCFG_SET_LOW_POWER: return "battery too low to write flash";
    case NETCFG_SET_WRITE_FAILED: return "nvs write failed";
  }
  return "unknown";
}

void netcfgPrint(Print &out) {
  for (size_t i = 0; i < NETCFG_FIELD_COUNT; i++) {
    const netcfg_entry_t &e = s_entries[i];
    const size_t len = strlen(e.buffer);

    if (!e.secret) {
      out.printf("%-6s %s%s\n", e.name, len ? e.buffer : "(empty)",
                 len ? "" : "  <- not set");
      continue;
    }

    // The length is the useful half of a secret: it is how a half-pasted key
    // is spotted without ever putting the key itself in a ticket.
    if (len)
      out.printf("%-6s (set, %u chars)\n", e.name, (unsigned)len);
    else
      out.printf("%-6s (empty)  <- not set\n", e.name);
  }
}
