#pragma once

#include <stddef.h>
#include <stdint.h>

// The device's own settings, their bounds, and the migrations between config
// versions. Pure C++ with no Arduino in sight, so every bound and every
// migration is exercised by the host tests in test/test_native.
//
// The bounds live here, next to the field. The dashboard and the gateway only
// display them: they can be older than the firmware and not know the new
// limits, so validation is always the device's job.

#define CFG_VERSION_CURRENT 3

// Layout invariant across every config version: freq_hz occupies bytes 0..3
// and cfg_version bytes 4..5. That makes any stored blob self-describing, so a
// record can be identified before it is interpreted.
//
// Fields are ordered wide-to-narrow so the struct has no implicit padding —
// a hole would carry indeterminate bytes into the CRC and make a
// byte-identical config look corrupt.

typedef struct { // 16 bytes
  uint32_t freq_hz;
  uint16_t cfg_version;
  uint16_t node_id;
  uint16_t period_s; // one period for everything; split in v2
  uint8_t log_level;
  uint8_t tx_power;
  uint8_t spreading;
  uint8_t _pad[3];
} config_v1_t;

typedef struct { // 20 bytes
  uint32_t freq_hz;
  uint16_t cfg_version;
  uint16_t node_id;
  uint16_t health_period_s;
  uint16_t ack_timeout_ms;
  uint16_t vbat_min_mv;
  uint8_t log_level;
  uint8_t tx_power;
  uint8_t spreading;
  uint8_t coding_rate;
  uint8_t sync_word;
  uint8_t ack_retries;
} config_v2_t;

// v3 adds the uplink: the node stops being a thing you visit and becomes a
// thing that reports. The credentials themselves are NOT here - see
// src/core/netcfg.h for why an SSID lives in a different store from a
// threshold.

typedef struct { // 28 bytes
  uint32_t freq_hz;
  uint16_t cfg_version;
  uint16_t node_id;
  uint16_t health_period_s; // over the air, to the neighbouring node
  uint16_t ack_timeout_ms;
  uint16_t vbat_min_mv;
  uint16_t report_period_s; // over WiFi, to the fleet service
  uint16_t ota_vbat_min_mv; // a far higher floor than a config write needs
  uint8_t log_level;
  uint8_t tx_power;
  uint8_t spreading;
  uint8_t coding_rate;
  uint8_t sync_word;
  uint8_t ack_retries;
  uint8_t wifi_enabled;
  uint8_t ota_enabled;
  uint8_t tls_verify;
  uint8_t _pad[1];
} config_v3_t;

typedef config_v3_t config_t;

typedef enum { CFG_U8, CFG_U16, CFG_U32 } cfg_type_t;

typedef struct {
  const char *name;
  cfg_type_t type;
  uint16_t offset;
  uint32_t min;
  uint32_t max;
  const char *unit;   // "dBm", "s", "ms", "mV", "Hz", or ""
  uint8_t applies_live; // 1 = takes effect at once, 0 = after a reboot
} cfg_field_t;

extern const cfg_field_t CFG_FIELDS[];
extern const size_t CFG_FIELD_COUNT;

// node_id has no sensible universal default — two boards sharing one id is a
// broken network — so the caller supplies it (derived from the chip MAC on
// first boot, or kept from the previous config).
void config_defaults(config_t *out, uint16_t node_id);

const cfg_field_t *config_field_by_name(const char *name);
uint32_t config_field_get(const config_t *c, const cfg_field_t *f);

// 0 on success, -1 when the value is outside the field's documented range.
// Nothing is written to *c unless the value is accepted.
int config_field_set(config_t *c, const cfg_field_t *f, uint32_t value);

// 0 when every field is in range. Otherwise -1, and *bad_field names the first
// offender so the refusal can explain itself.
int config_validate(const config_t *c, const char **bad_field);

// Walks a stored blob forward one version at a time until it is current.
// Returns 0 on success, -1 when the version is unknown or the blob is the
// wrong size for the version it claims.
//
// Never a memcpy of a whole old struct into a new one: that is how thresholds
// silently turn into random numbers. Every migration moves named fields and
// fills the new ones from the firmware's defaults.
int config_migrate(const void *blob, size_t blob_len, uint16_t from_version, config_t *out);

// Exposed so the tests can check one hop in isolation. Each hop writes the
// struct of the version it produces, never the current one: a chain that
// short-circuits to "current" is a chain with one untested link in it.
void config_migrate_1_2(const config_v1_t *in, config_v2_t *out);
void config_migrate_2_3(const config_v2_t *in, config_v3_t *out);
