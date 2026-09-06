#pragma once

#include <Arduino.h>

#include "ring.h"

// The device half of the fleet service's API. Four calls, all behind an
// X-Api-Key header:
//
//   POST /api/v1/devices/{serial}/health-reports   what this node is and how it is
//   POST /api/v1/devices/{serial}/log-records      a slice of the ring log
//   GET  /api/v1/devices/{serial}/target-firmware  what it should be running
//   GET  {downloadUrl}                             the image itself (see ota.h)
//
// Every function here is blocking and belongs to the network task alone. None
// of them touch the radio, the display or the config.

// What the fleet service says a node should be running, and everything the
// node needs to judge an image before it writes a byte of it.
typedef struct {
  char version[24];
  char git_hash[16];
  char build_type[12];
  char hardware_id[40];
  char sha256[65]; // 64 hex characters and a NUL
  char download_url[192];
  uint32_t proto_version;
  uint32_t size_bytes;
} ota_manifest_t;

typedef struct {
  bool update_available;
  char target_version[24];
} fleet_checkin_t;

// Negative values are HTTPClient's own errors (see HTTPC_ERROR_*); anything
// >= 100 is an HTTP status. FLEET_ERR_NOT_READY means the node has no
// credentials or no link, and no request was attempted.
constexpr int FLEET_ERR_NOT_READY = -1000;
constexpr int FLEET_ERR_BAD_RESPONSE = -1001;

// Certificate checking, the SNTP clock and the base URL are all read at call
// time from the config and the credential store, so `net set` and
// `config set tls_verify` take effect on the next request with no reboot.
int fleetPostHealth(fleet_checkin_t &out);

// Records are sent oldest first, so a batch that is cut short by a lost link
// leaves a contiguous gap at the end rather than holes in the middle.
int fleetPostLogs(const log_rec_t *records, uint16_t count);

// 200 with a filled manifest, 204 when the node is already on the right
// version - the common answer, and the cheapest one for a device to handle.
int fleetGetTargetFirmware(ota_manifest_t &out);

// Renders an HTTP status or a client error as something an operator can read.
const char *fleetErrorText(int status);
