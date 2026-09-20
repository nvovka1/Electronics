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

bool netIsStationConnected();
bool netIsAccessPoint();

// The address the board's own page is on, so the shell can print something a
// person can type into a browser.
String netAddress();

// The HTTP status of the last upload attempt, or a negative HTTPClient error
// code. 0 means nothing has been tried yet.
int netLastHttpStatus();

uint32_t netRowsUploaded();
