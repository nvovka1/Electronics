#pragma once

#include <Arduino.h>

// The uplink: WiFi, a clock, and a periodic conversation with the fleet
// service. Everything blocking about the network lives in this one task, so
// nothing it waits on can ever delay a key press, a frame or a redraw.
//
// This task is deliberately NOT subscribed to the task watchdog. Its work is
// dominated by calls that block for tens of seconds by design - a DNS lookup,
// a TLS handshake, a hosting instance waking from sleep - and a watchdog that
// fires on those turns somebody else's slow afternoon into a reboot loop on
// our node. It watches itself instead: consecutive failures drop the
// association and start again, and every attempt is in the log.

bool netBegin();
bool netTaskStart(UBaseType_t priority, BaseType_t core);

bool netIsConnected();

// True once SNTP has given the node a real date. Certificate validity is a
// claim about a window of time, so without this there is no way to check one -
// which is why an unsynced clock refuses an HTTPS request rather than making
// it blindly.
bool netTimeIsSynced();

// The last time the fleet service accepted a health report, in milliseconds
// since boot. Zero means it never has. This is the evidence an image on trial
// needs before it is allowed to keep its place.
uint32_t netLastCheckInMs();

// What the service says this node should be running, from the last check-in.
const char *netTargetVersion();
bool netUpdateAvailable();

// Wakes the task now instead of at the end of report_period_s.
void netRequestReport();

// Asks for a target-firmware check on the next pass. apply = true also
// downloads and installs it, subject to every gate in ota.h.
void netRequestUpdateCheck(bool apply);

// True when repeated brownout resets have made this node stop bringing WiFi up
// at all for this run of power. The uplink is the largest current draw on the
// board, so after it has knocked the supply over three times the node keeps
// itself alive and diagnosable instead of retrying into another reset.
bool netBrownoutHold();

void netPrintStatus(Print &out);
