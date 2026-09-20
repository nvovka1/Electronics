#pragma once

#include <stddef.h>
#include <stdint.h>

#include "snapshot.h"

// The columns. One definition, three consumers: the row renderer here, the
// row-to-JSON converter that feeds the upload, and the service that exports the
// CSV you download from the site.
//
// CSV_HEADER IS A CONTRACT with Initiator.Web's TelemetryCsv. A host test
// asserts this literal against the column table below, and a service test
// asserts the same literal on that side. Add a column to one half only and a
// test fails - which is the point, because the alternative is a spreadsheet
// that quietly gains a shifted column and nobody notices until the numbers have
// already been believed.

#define CSV_HEADER                                                                    \
  "index,tMs,utc,armed,mode,gpsFix,sats,hdop,lat,lon,altMsl,altRel,groundSpeed,"      \
  "airSpeed,climb,heading,cog,roll,pitch,yaw,throttle,batteryVoltage,batteryCurrent," \
  "batteryRemaining,consumedMah,rcRssi,wifiRssi,linkAgeMs"

struct CsvColumn {
  const char *name;
  // Only the wire format cares. A string column is quoted in the JSON that goes
  // up; everything else goes bare, and an empty cell becomes null either way.
  bool isString;
};

extern const CsvColumn csvColumns[];
extern const size_t csvColumnCount;

// The longest row this can produce, with room for the newline and terminator.
const size_t CsvRowMax = 320;

// Renders one row. Returns the length written, or 0 if it would not fit. The
// row ends in '\n': the file is a stream of rows and the newline belongs to the
// row, not to whoever is appending it.
size_t csvRenderRow(uint32_t index, const TelemetrySnapshot &snapshot, uint32_t nowMs,
                    char *out, size_t max);

// Turns one rendered row back into a JSON object keyed by the column names.
// Returns the length written, or 0 when the line is malformed or will not fit.
//
// Reading the file back rather than keeping the values in memory is deliberate:
// the thing uploaded is then, byte for byte, the thing that was recorded. An
// upload path with its own copy of the numbers is an upload path that can
// disagree with the file it claims to be sending.
size_t csvRowToJson(const char *line, size_t length, char *out, size_t max);

// The row's index, read from its first column, or UINT32_MAX if the line does
// not start with one. The uploader uses this instead of tracking indices
// separately - the file already knows.
uint32_t csvRowIndex(const char *line, size_t length);

// Formats microseconds since the epoch as ISO-8601 UTC with milliseconds.
// Exposed for the tests; there is no other reason for it to be public.
size_t csvFormatUtc(uint64_t unixTimeUs, char *out, size_t max);
