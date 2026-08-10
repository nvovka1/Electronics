/*
 * Copy this file to include/secrets.h and fill in your own values.
 * secrets.h is gitignored so your credentials never reach the repository.
 */
#pragma once

// ---- WiFi ----
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

// ---- TAK server ----
// Hostname or bare IP. For TLS this MUST match the CN of the server
// certificate you generated with makeCert.sh, or the handshake fails.
#define TAK_HOST "your-server.duckdns.org"

// 8088 = plaintext stcp input (bring-up).  8089 = TLS with client cert.
#define TAK_PORT 8088

// ---- Identity ----
// The callsign is what other operators see on the map.
#define TAK_CALLSIGN "ESP32-1"
