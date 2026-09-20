#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Everything the flight controller has told us, folded into one struct.
//
// MAVLink is a stream of small independent messages arriving at different
// rates. A row of CSV is a picture of the aircraft at one instant. This struct
// is what turns the first into the second: every message updates the fields it
// knows about, and the logger photographs the whole thing on its own schedule.
//
// UNKNOWN IS NOT ZERO. A field nobody has reported is NAN, or the UNKNOWN_*
// sentinel for the integers. It reaches the CSV as an empty cell. This matters
// more than it looks: a battery that reads 0.00 V and a battery nobody has
// mentioned are completely different situations, and once they are both zero in
// the file there is no getting them apart again.

const int8_t UNKNOWN_I8 = -1;
const int16_t UNKNOWN_I16 = -1;
const int32_t UNKNOWN_I32 = -1;

struct TelemetrySnapshot {
  // Wall clock, from SYSTEM_TIME - which is GPS time, and the only real clock
  // anywhere in this system. Zero until the flight controller has one to give,
  // which it cannot do before the GPS has a fix.
  uint64_t unixTimeUs;
  // millis() when unixTimeUs arrived, so a row can be stamped with the wall
  // clock plus however long ago that was.
  uint32_t unixTimeAtMs;

  bool haveHeartbeat;
  uint32_t lastHeartbeatMs;
  bool armed;
  uint32_t mode;  // ArduPilot custom_mode, raw: its meaning depends on frame type
  uint8_t systemStatus;

  int8_t gpsFix;
  int8_t sats;
  float hdop;
  double lat;
  double lon;
  float altMsl;
  float altRel;
  float groundSpeed;
  float airSpeed;
  float climb;
  float heading;
  float cog;
  float roll;
  float pitch;
  float yaw;
  int16_t throttle;

  float batteryVoltage;
  float batteryCurrent;
  int16_t batteryRemaining;
  int32_t consumedMah;

  int16_t rcRssi;
  // Not from MAVLink. The board's own WiFi signal, filled in by the logger,
  // because "the upload stopped" and "the aircraft flew out of WiFi range" are
  // the same event seen from two sides and the row should show which.
  int16_t wifiRssi;

  uint32_t messagesSeen;

  // When GLOBAL_POSITION_INT last arrived. GPS_RAW_INT carries a position too,
  // but an unfiltered one, so it is only allowed to fill the position fields
  // when the filtered estimate has gone quiet - which is exactly the case worth
  // recording, an EKF that has given up while the GPS is still reporting.
  uint32_t globalPositionAtMs;
};

void snapshotReset(TelemetrySnapshot &snapshot);

// Folds one decoded message in. `nowMs` is millis(); passed rather than read so
// the whole thing runs on the host with no clock.
//
// Declared on the raw id and payload rather than on mavlink_message_t so that
// this header costs nothing to include. The implementation includes MAVLink.
struct __mavlink_message;

void snapshotApply(TelemetrySnapshot &snapshot, const void *message, uint32_t nowMs);

// How long since the last heartbeat, or UINT32_MAX when there has never been
// one. This is the field that tells a reader whether to trust the rest of the
// row.
uint32_t snapshotLinkAgeMs(const TelemetrySnapshot &snapshot, uint32_t nowMs);
