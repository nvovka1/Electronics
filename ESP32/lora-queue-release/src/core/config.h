#pragma once

#include <Arduino.h>

#include "cfg_record.h"
#include "config_schema.h"

// Live configuration, persisted across two NVS records.
//
// The rule is the one OTA uses for firmware slots, applied to settings: a
// write always targets the record that is NOT currently in use, so at every
// instant of the write there is still a complete, valid config on the device.
// Pull the power halfway through and the half-written record fails its CRC or
// carries the older sequence number; either way the node comes back on the
// last good value rather than on defaults or on half of the new one.

typedef enum {
  CFG_SET_OK = 0,
  CFG_SET_UNKNOWN_FIELD,
  CFG_SET_OUT_OF_RANGE,
  CFG_SET_LOW_POWER,
  CFG_SET_WRITE_FAILED,
} cfg_set_result_t;

// Loads the config. False means nothing valid was found and the defaults are
// in use, which the POST reports as its NVS bit.
bool configBegin();

const config_t &config();

uint32_t configSeq();
cfg_slot_t configSlot();

// True when a stored record had to be walked forward to the current version.
// Reported by `version` and `config get` so an operator can see that this
// node's settings came from an older firmware.
bool configWasMigrated(uint16_t &fromVersion);

// Validates, then saves, then applies - in that order, and nothing at all
// happens if any step refuses. A half-applied config is worse than no change.
cfg_set_result_t configSet(const char *key, uint32_t value, uint32_t &oldValue);

// Restores the firmware's defaults. node_id is kept: it identifies the board,
// and resetting it would silently collide two nodes. Calibration and the
// serial live in another namespace and are not touched at all.
cfg_set_result_t configReset();

const char *configSetResultText(cfg_set_result_t r);

// The anti-brick gate, shared with everything else that writes flash.
// True when the pack has enough left to survive an erase/write cycle - or when
// the battery reading is not trustworthy, because refusing on a number we do
// not believe would lock an operator out of a device over a broken sensor.
bool configPowerAllowsWrite();

// Millivolts read at the last refused write, so the refusal can say why.
uint16_t configLastRefusedMillivolts();

#if BUILD_TEST_COMMANDS
// Writes a synthetic v1 record so the migration path can be demonstrated on a
// real board: reboot after calling this and the loader walks it 1 -> 2.
// Never compiled into the field image.
bool configSeedV1();
#endif
