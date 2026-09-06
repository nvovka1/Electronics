#pragma once

#include <Arduino.h>

// Where the node is told how to reach the network and the fleet service.
//
// These four strings are deliberately NOT in the A/B config record, for the
// same reason the serial number and the battery calibration are not:
//
//   * They are provisioning, not tuning. A threshold is changed by whoever is
//     operating the node; an SSID is set once when the node is commissioned.
//   * Together they are about 260 bytes. The config blob is 64, sized so the
//     whole record fits one NVS entry and the CRC covers a fixed shape. Growing
//     it five-fold to carry a WiFi password would change the record size, and a
//     changed record size is exactly what makes a node coming from the previous
//     firmware fall back to defaults - silently, which is the worst way for it
//     to happen.
//   * NVS already gives a single string key its own atomicity. The A/B scheme
//     exists because the config is one blob written as a unit; four independent
//     strings do not have that problem to solve.
//
// The compiled-in defaults come from build flags, so a factory-fresh board
// joins the network it was built for without anyone typing anything, and any
// node can still be moved to another network in the field with `net set`.
// A default is never written to NVS: that keeps `net reset` meaningful and
// keeps the fallback in one place - the image.

constexpr size_t NET_SSID_MAX = 32;  // 802.11 SSID, bytes not characters
constexpr size_t NET_PASS_MAX = 63;  // WPA2-PSK passphrase
constexpr size_t NET_URL_MAX = 96;
constexpr size_t NET_KEY_MAX = 64;

typedef enum {
  NETCFG_SSID = 0,
  NETCFG_PASS,
  NETCFG_URL,
  NETCFG_KEY,
  NETCFG_FIELD_COUNT,
} netcfg_field_t;

void netcfgBegin();

const char *netcfgSsid();
const char *netcfgPassword();
const char *netcfgBaseUrl();
const char *netcfgApiKey();

// True once there is enough to try: an SSID and a base URL. The API key can be
// empty on a service that does not require one, so it is not part of the test.
bool netcfgIsProvisioned();

typedef enum {
  NETCFG_SET_OK = 0,
  NETCFG_SET_UNKNOWN_FIELD,
  NETCFG_SET_TOO_LONG,
  NETCFG_SET_INVALID,
  NETCFG_SET_LOW_POWER,
  NETCFG_SET_WRITE_FAILED,
} netcfg_set_result_t;

netcfg_set_result_t netcfgSet(const char *field, const char *value);
netcfg_set_result_t netcfgReset();
const char *netcfgSetResultText(netcfg_set_result_t r);

// Never prints the password or the key. Both are secrets and a serial session
// is routinely pasted into a ticket; what an operator needs to know is whether
// one is set and how long it is, which is enough to spot a truncated paste.
void netcfgPrint(Print &out);
