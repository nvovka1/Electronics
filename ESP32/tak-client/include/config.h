/*
 * Non-secret configuration for the ESP32 TAK client.
 * WiFi credentials and the server hostname live in secrets.h (gitignored).
 */
#pragma once

#include <Arduino.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Copy include/secrets.example.h to include/secrets.h and fill it in."
#endif

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
// Uncomment to use mutual-TLS on port 8089 — the transport a real ATAK client
// uses, and the only one safe to expose on a public host. It needs certs.h
// (see certs.example.h) and a TAK_PORT of 8089 in secrets.h.
//
// Leave it commented for bring-up over the plaintext stcp input, which needs
// no certificates but is unauthenticated: restrict that port to your own
// source IP and remove the input once TLS works.
//
// #define TAK_USE_TLS 1

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// The CoT type for this contact. a-f-G-U-C = atom, friendly, ground, unit,
// combat — the ordinary "person on the map" icon. The second letter is the
// affiliation (f friend, h hostile, n neutral, u unknown) and the third is the
// battle dimension (G ground, A air, S sea surface).
#define TAK_COT_TYPE "a-f-G-U-C"

// Team colour and role, shown in ATAK's contact list.
// Colours: Cyan, Green, Red, Purple, Orange, Blue, Magenta, White, Maroon,
//          Dark Blue, Teal, Dark Green, Brown, Yellow
#define TAK_TEAM "Cyan"
#define TAK_ROLE "Team Member"

// Reported in the <takv> element; cosmetic, shown in the contact details.
#define TAK_PLATFORM "ESP32-TAK"
#define TAK_DEVICE   "TTGO LoRa32 V2.1"
#define TAK_OS       "Arduino"
#define TAK_VERSION  "0.1.0"

// ---------------------------------------------------------------------------
// Position — hardcoded test coordinates
// ---------------------------------------------------------------------------
// Kyiv, Maidan Nezalezhnosti. Change to somewhere you will recognise on the map.
constexpr double HOME_LAT = 50.4501;
constexpr double HOME_LON = 30.5234;
constexpr double HOME_HAE = 150.0;   // height above ellipsoid, metres

// The reported position walks a slow circle of this radius so you can tell a
// live feed from a frozen one at a glance. Set to 0.0 for a fixed point.
constexpr double DRIFT_RADIUS_M = 25.0;
constexpr double DRIFT_PERIOD_S = 180.0;   // one lap every 3 minutes

// ---------------------------------------------------------------------------
// Cadence
// ---------------------------------------------------------------------------
constexpr uint32_t REPORT_INTERVAL_MS = 5000;

// stale = now + REPORT_INTERVAL * this. Must be > 1 or every message expires
// before its successor arrives and the icon flickers or never appears at all.
constexpr uint32_t STALE_MULTIPLIER = 3;

// Reconnect backoff after a dropped connection.
constexpr uint32_t RECONNECT_MIN_MS = 1000;
constexpr uint32_t RECONNECT_MAX_MS = 30000;
