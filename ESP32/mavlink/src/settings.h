#pragma once

#include <Arduino.h>

// Everything this board knows about itself that survives a power cut, in NVS.
//
// The serial is here rather than compiled in so that one image serves every
// board: a board with no serial set derives one from its chip MAC, which means
// a factory-fresh board enrols itself correctly without anybody typing
// anything. That is the one field that has to match between the hardware and
// the service, so the safest value is one nobody has to type.

struct Settings {
  char serial[24];

  char ssid[33];
  char password[65];
  char baseUrl[128];
  char apiKey[80];

  uint32_t mavBaud;
  uint32_t logRateHz;

  // Uploading can be switched off without touching the logging. Useful on the
  // bench, where the interesting question is usually whether rows are being
  // written at all, and a service that is asleep adds twenty seconds of noise
  // to every attempt to find out.
  bool uploadEnabled;

  // Incremented once per boot, and also the flight number: a power-on is a new
  // flight, so the counter that survives a reboot is the only thing that can
  // name it. The board has no clock until the GPS gives it one.
  uint32_t bootCount;
};

extern Settings settings;

// Loads from NVS, filling anything unset from the build-time defaults in
// fleet.ini, and bumps the boot count. Call once, before any task starts.
void settingsLoad();

bool settingsSaveSerial(const char *serial);
bool settingsSaveWifi(const char *ssid, const char *password);
bool settingsSaveBaseUrl(const char *url);
bool settingsSaveApiKey(const char *key);
bool settingsSaveMavBaud(uint32_t baud);
bool settingsSaveLogRate(uint32_t hertz);
bool settingsSaveUploadEnabled(bool enabled);

// Back to what the image was built with. Does not touch the boot count: how
// many times this board has started is a fact about the board, not a setting,
// and resetting it would give two different flights the same number.
bool settingsReset();
