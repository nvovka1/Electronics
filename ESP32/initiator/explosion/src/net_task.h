#pragma once

// WiFi and the Initiator service: the periodic report, the command poll, and
// draining the transition and log buffers.
//
// Nothing here can delay a state transition. The state task never waits on this
// one, and a node with no network still applies commands from the radio, drives
// its LEDs and keeps its record - offline is a gap in the record, not a fault.

void netTaskStart();

bool netIsConnected();

// Lifts the after-a-brownout hold on the WiFi radio without a reboot, for when
// you have just fixed the supply and want to see it come up.
void netEnableWifi();

// True when WiFi is being kept off because the last reset was a brownout.
bool netWifiHeldOff();
