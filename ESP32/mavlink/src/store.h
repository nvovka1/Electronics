#pragma once

#include <stdint.h>

#include "flightlog.h"
#include "snapshot.h"

// The two things more than one task touches: the snapshot and the flight log.
//
// Everywhere else in this firmware a task owns its own state outright. These
// two cannot be: the snapshot is written by the MAVLink task and read by the
// logger and the web server, and the flight log is appended by the logger while
// being read by the uploader and the web server. So they live here, behind
// their own locks, and nothing else in the firmware needs a mutex.

bool storeBegin();

// Folds a decoded MAVLink message into the snapshot. MAVLink task only.
void storeApplyMessage(const void *message, uint32_t nowMs);

// A copy, taken under the lock. Callers get a snapshot they can read at leisure
// without holding anything, which is what makes the logger's timing
// independent of the message rate.
void storeSnapshot(TelemetrySnapshot &out);

void storeSetWifiRssi(int16_t rssi);

// The flight log's lock is held across several calls - list, read, then record
// the new cursor - so it is taken explicitly rather than hidden inside each
// one. A caller that cannot get it within the timeout gives up and tries later;
// nothing here is worth blocking a task for.
bool storeLockLog(uint32_t timeoutMs);
void storeUnlockLog();
FlightLog &storeFlightLog();

uint32_t storeFsTotalBytes();
uint32_t storeFsUsedBytes();
