#pragma once

#include <Arduino.h>

#include "events.h"
#include "log.h"

// Everything this node says to the Initiator service, and the one thing it asks
// it. Nothing outside this file builds a URL or a JSON body.
//
// Every call returns false on any failure and logs why. None of them throw and
// none of them block a state transition: the node applies commands and drives
// its LEDs whether or not any of this works.

struct PolledCommand {
  char commandId[COMMAND_ID_MAX];
  uint8_t command;
};

// POST the periodic report. Also the enrolment: the first one from a serial
// creates the device. `serviceState` receives what the backend believes this
// node's state is, so a disagreement can be noticed.
bool fleetPostHealth(uint8_t state, uint8_t *serviceState);

// POST buffered transitions. `stored` receives how many the backend kept, which
// is what may then be dropped from the buffer - never more.
bool fleetPostStateEvents(const StateEventRecord *events, uint8_t count, uint8_t *stored);

bool fleetPostLogs(const LogRecord *records, uint8_t count);

// GET the next queued command. Returns true only when there is one; a 204 is
// the common answer and is not a failure.
bool fleetPollCommand(PolledCommand *out);

// The HTTP status of the last request, or a negative HTTPClient error. Exposed
// so the screen can show it: on a board that browns out the moment a serial
// cable is plugged in, the OLED is the only place a diagnosis can appear.
//
// 0 means nothing has been attempted yet.
int fleetLastHttpStatus();
