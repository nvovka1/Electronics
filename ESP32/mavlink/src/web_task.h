#pragma once

// The board's own web server. Live telemetry, the list of flights, a CSV
// download per flight, and the recent log.
//
// It exists because the Initiator site cannot always be reached and the board
// can. At a field with no WiFi the board raises its own access point and this
// page is the only way to see whether anything is being recorded and to get the
// file off - which is precisely when it matters most.

void webTaskStart();
