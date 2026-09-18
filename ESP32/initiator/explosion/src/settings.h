#pragma once

#include <Arduino.h>

#include "node_state.h"

// Everything a node knows about itself that survives a power cut, in NVS.
//
// Deliberately NOT here: the current state. A node boots into SAFE every time,
// and restoring ARMED or FIRE from flash is precisely what must never happen.
// The replay counters are stored, because forgetting those is what would let an
// old recorded frame work again after a reboot.

struct Settings {
  uint16_t nodeId;
  char serial[16];
  uint32_t autoArmSeconds;

  char ssid[33];
  char password[65];
  char baseUrl[128];
  char apiKey[80];

  // WiFi can be switched off entirely, so the node runs on LoRa alone. Worth
  // having: the radio side is independent of the network, and a board whose
  // supply cannot survive the WiFi transmitter is still a perfectly good node
  // with a gap in its reporting.
  bool wifiEnabled;

  // WiFi transmit power in dBm. Lower means smaller current peaks, which is
  // what a marginal supply cannot survive. Settable so the threshold can be
  // found on the bench without a rebuild between every try.
  int8_t wifiTxPowerDbm;

  // Incremented once per boot. Sent with every report, because the node's
  // millisecond clock restarts at zero on each boot and the backend needs the
  // pair to order two reports.
  uint32_t bootCount;
};

extern Settings settings;

// Loads from NVS, filling anything unset from the build-time defaults, and
// bumps the boot count. Call once, before any task starts.
void settingsLoad();

bool settingsSaveNodeId(uint16_t nodeId);
bool settingsSaveAutoArmSeconds(uint32_t seconds);
bool settingsSaveWifi(const char *ssid, const char *password);
bool settingsSaveBaseUrl(const char *url);
bool settingsSaveApiKey(const char *key);
bool settingsSaveWifiEnabled(bool enabled);
bool settingsSaveWifiTxPower(int8_t dbm);

// Back to what the image was built with. Does not touch the boot count: how
// many times this board has started is a fact about the board, not a setting.
bool settingsReset();

// The highest command counter seen from a controller, kept across reboots.
uint32_t settingsLoadReplayCounter(uint16_t src);
void settingsSaveReplayCounter(uint16_t src, uint32_t counter);
