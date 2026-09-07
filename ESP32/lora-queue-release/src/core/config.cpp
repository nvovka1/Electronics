#include "core/config.h"

#include <Preferences.h>

#include "core/calib.h"
#include "core/log.h"
#include "hal/battery.h"

static constexpr const char *NS = "cfg";
static constexpr const char *KEY_A = "a";
static constexpr const char *KEY_B = "b";

static config_t s_cfg;
static uint32_t s_seq = 0;
static cfg_slot_t s_slot = CFG_SLOT_NONE;
static bool s_migrated = false;
static uint16_t s_migratedFrom = 0;
static uint16_t s_lastRefusedMv = 0;

static const char *slotKey(cfg_slot_t slot) {
  return (slot == CFG_SLOT_B) ? KEY_B : KEY_A;
}

// A node that was never given an id still needs a unique one, or two boards on
// one desk both answer to node 1. The chip MAC provides it until an operator
// sets a real one.
static uint16_t defaultNodeIdFromMac() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const uint16_t raw = (uint16_t)(((uint16_t)mac[4] << 8) | mac[5]);
  return (uint16_t)(1u + (raw % 999u)); // the field's documented range is 1..999
}

static bool readSlot(Preferences &prefs, cfg_slot_t slot, cfg_record_t &out) {
  const size_t n = prefs.getBytes(slotKey(slot), &out, sizeof(out));
  return n == sizeof(out);
}

static bool writeSlot(cfg_slot_t slot, const cfg_record_t &rec) {
  Preferences prefs;
  if (!prefs.begin(NS, /*readOnly=*/false)) return false;
  const size_t n = prefs.putBytes(slotKey(slot), &rec, sizeof(rec));
  prefs.end();
  return n == sizeof(rec);
}

// Builds the next record and writes it to the inactive slot. The active slot
// is never touched, so it stays a complete fallback for the whole write.
static bool persist(const config_t &cfg) {
  const cfg_slot_t target = cfg_record_next_slot(s_slot);

  cfg_record_t rec;
  cfg_record_build(&rec, s_seq + 1u, cfg.cfg_version, &cfg, sizeof(cfg));

  if (!writeSlot(target, rec)) {
    LOG_E(TAG_CFG, E_CFG_SAVE_FAIL, (uint32_t)target);
    return false;
  }

  // Only now does the new record become the active one. Up to this line a
  // power cut leaves the previous slot in charge.
  s_slot = target;
  s_seq = rec.seq;
  LOG_I(TAG_CFG, E_CFG_SAVED, ((uint32_t)target << 24) | (s_seq & 0xFFFFFFu));
  return true;
}

bool configBegin() {
  const uint16_t fallbackNodeId = defaultNodeIdFromMac();

  Preferences prefs;
  cfg_record_t a, b;
  bool aPresent = false, bPresent = false;

  if (prefs.begin(NS, /*readOnly=*/true)) {
    aPresent = readSlot(prefs, CFG_SLOT_A, a);
    bPresent = readSlot(prefs, CFG_SLOT_B, b);
    prefs.end();
  }

  // Report a slot that is present but broken: it is the visible evidence that
  // a write was interrupted, and it is what the power-cut experiment looks for.
  if (aPresent && !cfg_record_valid(&a)) LOG_W(TAG_CFG, E_CFG_SLOT_BAD, CFG_SLOT_A);
  if (bPresent && !cfg_record_valid(&b)) LOG_W(TAG_CFG, E_CFG_SLOT_BAD, CFG_SLOT_B);

  const cfg_slot_t pick = cfg_record_pick(aPresent ? &a : nullptr, aPresent,
                                          bPresent ? &b : nullptr, bPresent);
  if (pick == CFG_SLOT_NONE) {
    config_defaults(&s_cfg, fallbackNodeId);
    s_seq = 0;
    s_slot = CFG_SLOT_NONE;
    LOG_W(TAG_CFG, E_CFG_DEFAULTS, 0);
    logSetLevel(s_cfg.log_level);
    return false;
  }

  const cfg_record_t &chosen = (pick == CFG_SLOT_A) ? a : b;

  // Never a memcpy of the whole stored struct: a v1 blob read as a v2 struct
  // turns settings into whatever happened to be next in flash, and does it
  // silently.
  config_t loaded;
  if (config_migrate(chosen.blob, chosen.size, chosen.cfg_version, &loaded) != 0) {
    config_defaults(&s_cfg, fallbackNodeId);
    s_seq = chosen.seq;
    s_slot = CFG_SLOT_NONE;
    LOG_E(TAG_CFG, E_CFG_INVALID, chosen.cfg_version);
    logSetLevel(s_cfg.log_level);
    return false;
  }

  const char *bad = nullptr;
  if (config_validate(&loaded, &bad) != 0) {
    // The record's CRC was good, so this is not a torn write - it is a config
    // this firmware considers impossible. Defaults are the safe answer.
    config_defaults(&s_cfg, fallbackNodeId);
    s_seq = chosen.seq;
    s_slot = CFG_SLOT_NONE;
    LOG_E(TAG_CFG, E_CFG_INVALID, 0);
    logSetLevel(s_cfg.log_level);
    return false;
  }

  s_cfg = loaded;
  s_seq = chosen.seq;
  s_slot = pick;
  logSetLevel(s_cfg.log_level);

  LOG_I(TAG_CFG, E_CFG_LOADED, ((uint32_t)pick << 24) | (s_seq & 0xFFFFFFu));

  if (chosen.cfg_version != CFG_VERSION_CURRENT) {
    s_migrated = true;
    s_migratedFrom = chosen.cfg_version;
    LOG_I(TAG_CFG, E_CFG_MIGRATED,
          ((uint32_t)chosen.cfg_version << 16) | CFG_VERSION_CURRENT);
    // Written back at the new version so the walk happens once, not on every
    // boot for the rest of the node's life.
    persist(s_cfg);
  }

  return true;
}

const config_t &config() { return s_cfg; }
uint32_t configSeq() { return s_seq; }
cfg_slot_t configSlot() { return s_slot; }
uint16_t configLastRefusedMillivolts() { return s_lastRefusedMv; }

bool configWasMigrated(uint16_t &fromVersion) {
  fromVersion = s_migratedFrom;
  return s_migrated;
}

// The anti-brick gate. Writing flash is the hungriest thing this firmware
// does, and doing it on a flat pack is how a device stops coming back. The
// same rule that forbids OTA below 40% forbids a config write below the
// configured floor.
bool configPowerAllowsWrite() {
  // Nothing to protect against on a node that cannot run out of charge.
  if (!s_cfg.has_battery) return true;

  if (!batteryTrusted()) {
    // The ADC self-test failed, so the reading means nothing. Refusing on a
    // number we do not believe would lock the operator out of the device over
    // a broken sensor, so the gate opens and says so.
    LOG_W(TAG_BATT, E_BATT_UNTRUSTED, 0);
    return true;
  }

  const uint16_t mv = batteryMillivolts();
  if (mv >= s_cfg.vbat_min_mv) return true;

  s_lastRefusedMv = mv;
  LOG_W(TAG_BATT, E_LOWBAT_WRITE_BLOCKED, mv);
  return false;
}

cfg_set_result_t configSet(const char *key, uint32_t value, uint32_t &oldValue) {
  const cfg_field_t *field = config_field_by_name(key);
  if (!field) return CFG_SET_UNKNOWN_FIELD;

  oldValue = config_field_get(&s_cfg, field);

  // Validate against a copy. Nothing reaches the live config until the write
  // has actually succeeded.
  config_t candidate = s_cfg;
  if (config_field_set(&candidate, field, value) != 0) return CFG_SET_OUT_OF_RANGE;

  const char *bad = nullptr;
  if (config_validate(&candidate, &bad) != 0) return CFG_SET_OUT_OF_RANGE;

  if (!configPowerAllowsWrite()) return CFG_SET_LOW_POWER;

  const config_t previous = s_cfg;
  s_cfg = candidate;
  if (!persist(s_cfg)) {
    s_cfg = previous; // the flash refused, so RAM must not drift away from it
    return CFG_SET_WRITE_FAILED;
  }

  // Every change is an event with a time and a value. In a month this is the
  // only way to answer "why is this node's power different".
  LOG_I(TAG_CFG, E_CFG_CHANGED,
        ((uint32_t)(field - CFG_FIELDS) << 24) | (value & 0xFFFFFFu));

  if (field->applies_live && strcmp(field->name, "log_level") == 0)
    logSetLevel(s_cfg.log_level);

  return CFG_SET_OK;
}

cfg_set_result_t configReset() {
  if (!configPowerAllowsWrite()) return CFG_SET_LOW_POWER;

  const config_t previous = s_cfg;

  // node_id survives: it names the board rather than tuning it, and resetting
  // it would put two nodes on the same address without anyone noticing.
  config_defaults(&s_cfg, previous.node_id);

  if (!persist(s_cfg)) {
    s_cfg = previous;
    return CFG_SET_WRITE_FAILED;
  }

  logSetLevel(s_cfg.log_level);
  LOG_I(TAG_CFG, E_CFG_RESET, 0);
  return CFG_SET_OK;
}

const char *configSetResultText(cfg_set_result_t r) {
  switch (r) {
    case CFG_SET_OK: return "ok";
    case CFG_SET_UNKNOWN_FIELD: return "unknown field";
    case CFG_SET_OUT_OF_RANGE: return "out of range";
    case CFG_SET_LOW_POWER: return "battery too low to write flash";
    case CFG_SET_WRITE_FAILED: return "nvs write failed";
  }
  return "unknown";
}

#if BUILD_TEST_COMMANDS
bool configSeedV1() {
  // A believable old config: values an operator would actually have set, so
  // the migration can be seen carrying them across rather than producing
  // defaults that look the same either way.
  config_v1_t v1;
  memset(&v1, 0, sizeof(v1));
  v1.cfg_version = 1;
  v1.freq_hz = s_cfg.freq_hz;
  v1.node_id = s_cfg.node_id;
  v1.period_s = 120;
  v1.log_level = 4;
  v1.tx_power = 11;
  v1.spreading = 9;

  cfg_record_t rec;
  cfg_record_build(&rec, s_seq + 1u, 1, &v1, sizeof(v1));

  const cfg_slot_t target = cfg_record_next_slot(s_slot);
  if (!writeSlot(target, rec)) return false;

  s_slot = target;
  s_seq = rec.seq;
  return true;
}
#endif
