#include "config_schema.h"

#include <string.h>

#define F(member) (uint16_t) offsetof(config_t, member)

// One table drives the shell parser, the `config get` dump and the protocol
// spec's config section. Two tables would disagree by the third week.
//
// cfg_version is deliberately absent: it is not a setting, it is the shape of
// the record. It is shown by `config get` but can never be assigned.
const cfg_field_t CFG_FIELDS[] = {
    // name             type     offset                min         max     unit   live
    {"node_id", CFG_U16, F(node_id), 1, 999, "", 0},
    {"log_level", CFG_U8, F(log_level), 0, 5, "", 1},
    {"tx_power", CFG_U8, F(tx_power), 2, 20, "dBm", 1},
    {"spreading", CFG_U8, F(spreading), 7, 12, "SF", 0},
    {"coding_rate", CFG_U8, F(coding_rate), 5, 8, "4/x", 0},
    {"sync_word", CFG_U8, F(sync_word), 0x01, 0xFF, "", 0},
    {"freq_hz", CFG_U32, F(freq_hz), 863000000u, 870000000u, "Hz", 0},
    {"health_period_s", CFG_U16, F(health_period_s), 10, 3600, "s", 1},
    {"ack_timeout_ms", CFG_U16, F(ack_timeout_ms), 100, 5000, "ms", 1},
    {"ack_retries", CFG_U8, F(ack_retries), 0, 5, "", 1},
    {"vbat_min_mv", CFG_U16, F(vbat_min_mv), 3000, 4200, "mV", 1},
    {"wifi_enabled", CFG_U8, F(wifi_enabled), 0, 1, "", 0},
    // Six hours is the ceiling because the field is a uint16_t and because a
    // node heard from less often than that is not a managed node. It is also
    // the worst case for how long an update takes to reach the fleet.
    {"report_period_s", CFG_U16, F(report_period_s), 30, 21600, "s", 1},
    {"tls_verify", CFG_U8, F(tls_verify), 0, 1, "", 0},
    {"ota_enabled", CFG_U8, F(ota_enabled), 0, 1, "", 1},
    {"ota_vbat_min_mv", CFG_U16, F(ota_vbat_min_mv), 3300, 4200, "mV", 1},
};

const size_t CFG_FIELD_COUNT = sizeof(CFG_FIELDS) / sizeof(CFG_FIELDS[0]);

void config_defaults(config_t *out, uint16_t node_id) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->cfg_version = CFG_VERSION_CURRENT;
  out->node_id = node_id;
  out->freq_hz = 868000000u;
  out->health_period_s = 60;
  out->ack_timeout_ms = 600;
  out->vbat_min_mv = 3300;
  out->log_level = 3; // INFO
  out->tx_power = 14;
  out->spreading = 7;
  out->coding_rate = 5;
  // Not the library's stock 0x12: with that word every other off-the-shelf
  // node in range is heard and rendered as if it were ours.
  out->sync_word = 0x2B;
  out->ack_retries = 3;

  out->wifi_enabled = 1;
  // Five minutes, not one. A check-in is a WiFi association, a TLS handshake
  // and two POSTs; at one a minute the radio is awake most of the time and the
  // fleet service is answering 1440 requests a day per node for facts that
  // change hourly.
  out->report_period_s = 300;
  out->tls_verify = 1;
  out->ota_enabled = 1;
  // 3.6 V, well above the 3.3 V a config write needs. An OTA writes a third of
  // a megabyte and then reboots into an image that has never run: the one
  // moment in this device's life where a flat cell turns a bug into a brick.
  out->ota_vbat_min_mv = 3600;
}

const cfg_field_t *config_field_by_name(const char *name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < CFG_FIELD_COUNT; i++)
    if (strcmp(CFG_FIELDS[i].name, name) == 0) return &CFG_FIELDS[i];
  return nullptr;
}

static uint8_t *field_ptr(config_t *c, const cfg_field_t *f) {
  return (uint8_t *)c + f->offset;
}

uint32_t config_field_get(const config_t *c, const cfg_field_t *f) {
  if (!c || !f) return 0;
  const uint8_t *p = (const uint8_t *)c + f->offset;
  switch (f->type) {
    case CFG_U8: {
      uint8_t v;
      memcpy(&v, p, sizeof(v));
      return v;
    }
    case CFG_U16: {
      uint16_t v;
      memcpy(&v, p, sizeof(v));
      return v;
    }
    case CFG_U32: {
      uint32_t v;
      memcpy(&v, p, sizeof(v));
      return v;
    }
  }
  return 0;
}

int config_field_set(config_t *c, const cfg_field_t *f, uint32_t value) {
  if (!c || !f) return -1;
  if (value < f->min || value > f->max) return -1;

  uint8_t *p = field_ptr(c, f);
  switch (f->type) {
    case CFG_U8: {
      const uint8_t v = (uint8_t)value;
      memcpy(p, &v, sizeof(v));
      return 0;
    }
    case CFG_U16: {
      const uint16_t v = (uint16_t)value;
      memcpy(p, &v, sizeof(v));
      return 0;
    }
    case CFG_U32: {
      memcpy(p, &value, sizeof(value));
      return 0;
    }
  }
  return -1;
}

int config_validate(const config_t *c, const char **bad_field) {
  if (!c) {
    if (bad_field) *bad_field = "(null)";
    return -1;
  }
  if (c->cfg_version != CFG_VERSION_CURRENT) {
    if (bad_field) *bad_field = "cfg_version";
    return -1;
  }
  for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
    const uint32_t v = config_field_get(c, &CFG_FIELDS[i]);
    if (v < CFG_FIELDS[i].min || v > CFG_FIELDS[i].max) {
      if (bad_field) *bad_field = CFG_FIELDS[i].name;
      return -1;
    }
  }
  if (bad_field) *bad_field = nullptr;
  return 0;
}

// --- migrations -----------------------------------------------------------

static void defaults_v2(config_v2_t *out, uint16_t node_id) {
  // The v2 defaults as fw 1.0.0 shipped them, frozen. They must not follow
  // config_defaults() forward: a v1 record migrating today has to arrive at
  // the v2 this firmware's 2->3 hop expects, not at whatever v4 will call a
  // default.
  memset(out, 0, sizeof(*out));
  out->cfg_version = 2;
  out->node_id = node_id;
  out->freq_hz = 868000000u;
  out->health_period_s = 60;
  out->ack_timeout_ms = 600;
  out->vbat_min_mv = 3300;
  out->log_level = 3;
  out->tx_power = 14;
  out->spreading = 7;
  out->coding_rate = 5;
  out->sync_word = 0x2B;
  out->ack_retries = 3;
}

void config_migrate_1_2(const config_v1_t *in, config_v2_t *out) {
  if (!in || !out) return;

  // Start from the version's own defaults, then overwrite what v1 actually
  // knew. A new field therefore takes its value from the code, never a zero
  // out of flash — zero is reliably the worst possible setting.
  defaults_v2(out, in->node_id);

  out->freq_hz = in->freq_hz;
  out->log_level = in->log_level;
  out->tx_power = in->tx_power;
  out->spreading = in->spreading;
  out->health_period_s = in->period_s; // the one period v1 had becomes the health period

  out->cfg_version = 2;
}

void config_migrate_2_3(const config_v2_t *in, config_v3_t *out) {
  if (!in || !out) return;

  config_defaults(out, in->node_id);

  out->freq_hz = in->freq_hz;
  out->log_level = in->log_level;
  out->tx_power = in->tx_power;
  out->spreading = in->spreading;
  out->coding_rate = in->coding_rate;
  out->sync_word = in->sync_word;
  out->health_period_s = in->health_period_s;
  out->ack_timeout_ms = in->ack_timeout_ms;
  out->ack_retries = in->ack_retries;
  out->vbat_min_mv = in->vbat_min_mv;

  // wifi_enabled, report_period_s, tls_verify, ota_enabled and ota_vbat_min_mv
  // are new in v3 and keep the defaults config_defaults() just wrote. Note
  // what this means in practice: a node updated from 1.0.0 comes up with the
  // uplink ON. That is a deliberate choice — a fleet whose nodes have to be
  // visited once each to enable reporting is a fleet that never reports — and
  // it is safe only because the credentials are empty until somebody sets
  // them, so an unconfigured node simply finds no network and says so.

  out->cfg_version = 3;
}

int config_migrate(const void *blob, size_t blob_len, uint16_t from_version, config_t *out) {
  if (!blob || !out) return -1;

  // The chain is walked one hop at a time, so a node that sat in a drawer
  // through two releases arrives current by the same code path a one-hop
  // upgrade uses. No shortcut from 1 straight to 3: the shortcut is the link
  // nobody tests and the one that quietly drops a field.
  config_v2_t v2;

  switch (from_version) {
    case 1: {
      if (blob_len != sizeof(config_v1_t)) return -1;
      config_v1_t v1;
      memcpy(&v1, blob, sizeof(v1));
      if (v1.cfg_version != 1) return -1;
      config_migrate_1_2(&v1, &v2);
      config_migrate_2_3(&v2, out);
      return 0;
    }
    case 2: {
      if (blob_len != sizeof(config_v2_t)) return -1;
      memcpy(&v2, blob, sizeof(v2));
      if (v2.cfg_version != 2) return -1;
      config_migrate_2_3(&v2, out);
      return 0;
    }
    case CFG_VERSION_CURRENT: {
      if (blob_len != sizeof(config_t)) return -1;
      memcpy(out, blob, sizeof(*out));
      return 0;
    }
    default:
      // A config newer than this firmware. Refusing is the honest answer:
      // guessing at fields we do not know would corrupt them.
      return -1;
  }
}
