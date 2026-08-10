/*
 * Cursor-on-Target (CoT) event construction.
 *
 * CoT is the XML message format every TAK client speaks. There is no separate
 * "registration" message: the server creates a contact from the uid and
 * callsign carried inside the first position report, and keeps it alive only
 * while fresh reports keep arriving before each message's stale timestamp.
 * So sending buildPli() on a timer IS registration plus position reporting.
 */
#pragma once

#include <Arduino.h>

struct GeoPosition {
  double lat;      // degrees, WGS-84
  double lon;      // degrees, WGS-84
  double hae;      // height above ellipsoid, metres
  double ce;       // circular (horizontal) error, metres
  double le;       // linear (vertical) error, metres
  double speed;    // metres/second
  double course;   // degrees true, 0..360
};

namespace cot {

// CoT's sentinel for "accuracy not known". This is the documented convention,
// not an error value — sending 0.0 instead would claim perfect accuracy.
constexpr double UNKNOWN_ACCURACY = 9999999.0;

// Stable per-board identity derived from the eFuse MAC, e.g. "ESP32-A1B2C3D4E5F6".
// Stability across reboots is what makes the server treat us as the same
// contact; a random or uptime-derived uid spawns a fresh ghost every power
// cycle and litters the map.
const char* deviceUid();

// Milliseconds since the Unix epoch, UTC.
int64_t nowMillis();

// False until NTP has set the clock. CoT timestamps are absolute UTC, so
// sending with the ESP32's 1970 boot clock produces messages that are already
// stale on arrival — the server accepts them and nothing ever appears on the
// map, with no error to explain why.
bool clockIsSane();

// "2026-08-09T12:00:00.000Z"
String isoTimestamp(int64_t epochMillis);

// Self position report. type a-f-G-U-C, how m-g (from a GPS receiver).
String buildPli(const GeoPosition& pos, uint32_t staleSeconds);

// Connection heartbeat, type t-x-c-t. The server answers with t-x-c-t-r.
String buildPing();

}  // namespace cot
