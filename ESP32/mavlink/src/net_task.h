#pragma once

#include <Arduino.h>

// WiFi and the Initiator service. Station first, own access point if the saved
// network cannot be joined.
//
// Nothing here can affect the recording. The uploader reads a file the logger
// has already written and moves a cursor when the service acknowledges bytes;
// a wrong key, a sleeping instance or no network at all is a delay in the
// record, never a hole in it.

void netTaskStart();

// Keeps the radio off for this boot. Called when the last reset was a brownout:
// bringing WiFi up is what collapsed the supply, so doing it again immediately
// is a boot loop, and a board in a boot loop records nothing at all.
//
// Deliberately not persistent. A clean power-on clears it, so the way to get
// the radio back after fixing the supply is to switch the board off and on -
// which is what anyone would do anyway.
void netHoldWifi();

// Lifts the hold without a reboot, for when the supply has just been fixed and
// you want to see whether it worked.
void netEnableWifi();

bool netWifiHeldOff();

bool netIsStationConnected();
bool netIsAccessPoint();

// The address the board's own page is on, so the shell can print something a
// person can type into a browser.
String netAddress();

// The HTTP status of the last upload attempt, or a negative HTTPClient error
// code. 0 means nothing has been tried yet.
int netLastHttpStatus();

uint32_t netRowsUploaded();
