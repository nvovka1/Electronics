#include "log_task.h"

#include <Arduino.h>

#include "config.h"
#include "csv_row.h"
#include "log.h"
#include "settings.h"
#include "store.h"

static uint16_t _flightId = 0;
static uint32_t _rowsWritten = 0;
static uint32_t _writeFailures = 0;

static void logTask(void *) {
  // The boot count is the flight number. A power-on is a new flight, and with
  // no clock until the GPS provides one, a counter that survives the reboot is
  // the only thing available to name it by.
  _flightId = (uint16_t)(settings.bootCount & 0xffff);

  if (storeLockLog(2000)) {
    if (storeFlightLog().openFlight(_flightId, CSV_HEADER)) {
      LOG_INFO("rec", "flight %u open", (unsigned)_flightId);
    } else {
      LOG_ERROR("rec", "flight %u could not be opened", (unsigned)_flightId);
    }
    storeUnlockLog();
  } else {
    LOG_ERROR("rec", "flight log busy at start");
  }

  TelemetrySnapshot snapshot;
  char row[CsvRowMax];
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    // Read every tick, so `set rate` takes effect without a reboot.
    const uint32_t periodMs = 1000 / (settings.logRateHz == 0 ? 1 : settings.logRateHz);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(periodMs));

    storeSnapshot(snapshot);

    // Read BEFORE waiting for the lock, so a row delayed by contention still
    // carries the instant it describes rather than the instant it was written.
    // That is what makes waiting for the lock safe: the row is late, not wrong.
    //
    // Two seconds rather than a fifth of one, because the uploader can hold the
    // lock while it walks the flight directory. The old timeout gave up inside
    // that window and dropped four rows in ten - a hole in the record caused by
    // the network, which is the one coupling this whole design exists to avoid.
    const uint32_t now = millis();

    if (!storeLockLog(2000)) {
      _writeFailures++;
      continue;
    }

    FlightLog &flightLog = storeFlightLog();

    if (!flightLog.hasOpenFlight()) {
      storeUnlockLog();
      continue;
    }

    const size_t length = csvRenderRow(flightLog.nextIndex(), snapshot, now, row, sizeof(row));
    if (length == 0) {
      _writeFailures++;
      storeUnlockLog();
      continue;
    }

    if (flightLog.appendRow(row, (uint32_t)length)) {
      _rowsWritten++;
    } else {
      _writeFailures++;
      // Every 64th, because a filesystem that has started refusing writes
      // refuses every one of them and the log is the thing a person is reading
      // to find that out.
      if (_writeFailures % 64 == 1) {
        LOG_ERROR("rec", "append failed (%lu so far)", (unsigned long)_writeFailures);
      }
    }

    storeUnlockLog();
  }
}

// 6 kB rather than 4: appending a row can end up in dropOldest, which lists the
// directory into a 1.3 kB array of entries and a 0.5 kB array of flights. That
// path runs only when the disk is nearly full - exactly when a stack overflow
// would be least welcome and hardest to reproduce.
void logTaskStart() { xTaskCreatePinnedToCore(logTask, "rec", 6144, nullptr, 2, nullptr, 1); }

uint16_t logFlightId() { return _flightId; }
uint32_t logRowsWritten() { return _rowsWritten; }
uint32_t logWriteFailures() { return _writeFailures; }
