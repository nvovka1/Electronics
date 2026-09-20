#pragma once

#include <stdint.h>

// Tunables. Anything a person might want to change per board lives in NVS and
// is set over the serial shell; anything here is a decision about the firmware.

// ------------------------------------------------------------------- MAVLink

// UART2. UART0 is the console and the flashing port, so it is not available,
// and UART1's default pins collide with the flash on some modules.
static const int MavUartNumber = 2;

// 57600 is ArduPilot's default for a telemetry serial port. Settable, because
// the other common choice is 115200 and finding out which one a board is using
// should not need a rebuild.
static const uint32_t MavBaudDefault = 57600;

// We are a ground station as far as the flight controller is concerned. 255 is
// the conventional GCS system id and 190 the conventional "some other ground
// station" component id; using the autopilot's own ids would make our
// heartbeats indistinguishable from its own.
static const uint8_t MavOurSystemId = 255;
static const uint8_t MavOurComponentId = 190;

// How often we announce ourselves. ArduPilot streams telemetry regardless, but
// a silent link makes it assume the ground station has gone and stop some
// streams, so this is not optional.
static const uint32_t MavHeartbeatIntervalMs = 1000;

// Re-requesting the message intervals every so often. ArduPilot forgets them
// across its own reboot, and a flight controller that restarts mid-session
// would otherwise fall back to whatever SR*_ parameters say and quietly halve
// the log's resolution.
static const uint32_t MavRequestIntervalMs = 15000;

// --------------------------------------------------------------- the logger

// Rows per second. Two is the default: about 120 bytes a row, so roughly five
// hours of flight in the 2 MB filesystem, and fast enough that a crash is still
// legible half a second at a time.
static const uint32_t LogRateHzDefault = 2;
static const uint32_t LogRateHzMax = 10;

// Free space never knowingly consumed. LittleFS needs room to manoeuvre, and a
// filesystem run to its last byte fails writes in ways much harder to recover
// from than one deleted old flight.
static const uint32_t FsReserveBytes = 64 * 1024;

// ---------------------------------------------------------------- the upload

// CSV bytes per batch, before conversion to JSON. Roughly 25 rows, which lands
// under 10 kB of JSON - comfortably within heap, and small enough that a failed
// request is cheap to repeat.
static const uint32_t UploadChunkBytes = 3072;

// Render's free instance sleeps after about fifteen minutes and takes tens of
// seconds to wake. A five-second timeout would classify every first request
// after a quiet spell as a failure.
static const uint32_t HttpTimeoutMs = 20000;

static const uint32_t UploadIdleDelayMs = 5000;
static const uint32_t UploadBackoffMsMax = 60000;

// -------------------------------------------------------------------- WiFi

static const uint32_t WifiJoinTimeoutMs = 20000;

// The fallback access point. Raised only when the saved network cannot be
// joined, so that a board at a field with no known WiFi is still reachable from
// a phone and the flights can still be pulled off it.
static const char *const ApSsid = "mavlink-telemetry";
static const char *const ApPassword = "telemetry";
