#include "csv_row.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const CsvColumn csvColumns[] = {
    {"index", false},          {"tMs", false},
    {"utc", true},             {"armed", false},
    {"mode", false},           {"gpsFix", false},
    {"sats", false},           {"hdop", false},
    {"lat", false},            {"lon", false},
    {"altMsl", false},         {"altRel", false},
    {"groundSpeed", false},    {"airSpeed", false},
    {"climb", false},          {"heading", false},
    {"cog", false},            {"roll", false},
    {"pitch", false},          {"yaw", false},
    {"throttle", false},       {"batteryVoltage", false},
    {"batteryCurrent", false}, {"batteryRemaining", false},
    {"consumedMah", false},    {"rcRssi", false},
    {"wifiRssi", false},       {"linkAgeMs", false},
};

const size_t csvColumnCount = sizeof(csvColumns) / sizeof(csvColumns[0]);

// --------------------------------------------------------------- the writer
// Every append checks the remaining space and gives up on the whole row rather
// than emitting a truncated one. A short row is worse than no row: it parses,
// it just means something else.

namespace {

struct Writer {
  char *out;
  size_t max;
  size_t length;
  bool ok;
};

void writeText(Writer &writer, const char *text) {
  if (!writer.ok) return;
  const size_t textLength = strlen(text);
  if (writer.length + textLength + 1 > writer.max) {
    writer.ok = false;
    return;
  }
  memcpy(writer.out + writer.length, text, textLength);
  writer.length += textLength;
  writer.out[writer.length] = 0;
}

void writeSpan(Writer &writer, const char *text, size_t textLength) {
  if (!writer.ok) return;
  if (writer.length + textLength + 1 > writer.max) {
    writer.ok = false;
    return;
  }
  memcpy(writer.out + writer.length, text, textLength);
  writer.length += textLength;
  writer.out[writer.length] = 0;
}

void writeSeparator(Writer &writer) { writeText(writer, ","); }

void writeInteger(Writer &writer, long long value, bool known) {
  if (!known) return;
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lld", value);
  writeText(writer, buffer);
}

void writeReal(Writer &writer, double value, int decimals) {
  if (isnan(value)) return;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  writeText(writer, buffer);
}

}  // namespace

// ------------------------------------------------------------------- clock
// Howard Hinnant's days-to-civil, rather than gmtime_r. The host build is not
// always glibc and the reentrant variants are exactly the ones that go missing;
// this is fifteen lines, has no locale, no time zone and no static state, and
// gives the same answer on the board and in the tests.

static void civilFromDays(int64_t days, int32_t &year, uint32_t &month, uint32_t &day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const uint64_t dayOfEra = (uint64_t)(days - era * 146097);
  const uint64_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const int64_t candidateYear = (int64_t)yearOfEra + era * 400;
  const uint64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const uint64_t monthPrime = (5 * dayOfYear + 2) / 153;

  day = (uint32_t)(dayOfYear - (153 * monthPrime + 2) / 5 + 1);
  month = (uint32_t)(monthPrime < 10 ? monthPrime + 3 : monthPrime - 9);
  year = (int32_t)(candidateYear + (month <= 2 ? 1 : 0));
}

size_t csvFormatUtc(uint64_t unixTimeUs, char *out, size_t max) {
  if (out == nullptr || max < 25) return 0;

  const uint64_t totalMilliseconds = unixTimeUs / 1000ULL;
  const uint64_t totalSeconds = totalMilliseconds / 1000ULL;
  const uint32_t milliseconds = (uint32_t)(totalMilliseconds % 1000ULL);

  const int64_t days = (int64_t)(totalSeconds / 86400ULL);
  const uint32_t secondOfDay = (uint32_t)(totalSeconds % 86400ULL);

  int32_t year = 0;
  uint32_t month = 0;
  uint32_t day = 0;
  civilFromDays(days, year, month, day);

  const int written = snprintf(out, max, "%04d-%02u-%02uT%02u:%02u:%02u.%03uZ", (int)year,
                               (unsigned)month, (unsigned)day, (unsigned)(secondOfDay / 3600),
                               (unsigned)((secondOfDay % 3600) / 60), (unsigned)(secondOfDay % 60),
                               (unsigned)milliseconds);

  return written > 0 ? (size_t)written : 0;
}

// -------------------------------------------------------------------- rows

size_t csvRenderRow(uint32_t index, const TelemetrySnapshot &snapshot, uint32_t nowMs, char *out,
                    size_t max) {
  if (out == nullptr || max == 0) return 0;

  Writer writer = {out, max, 0, true};
  out[0] = 0;

  writeInteger(writer, index, true);
  writeSeparator(writer);
  writeInteger(writer, nowMs, true);
  writeSeparator(writer);

  // Wall clock at this instant: what the flight controller said, plus however
  // long ago it said it. Empty until the GPS has given the aircraft a clock,
  // which is the honest answer - the board itself has no idea what time it is.
  if (snapshot.unixTimeUs != 0) {
    const uint64_t elapsedUs = (uint64_t)(nowMs - snapshot.unixTimeAtMs) * 1000ULL;
    char utc[32];
    if (csvFormatUtc(snapshot.unixTimeUs + elapsedUs, utc, sizeof(utc)) > 0) {
      writeText(writer, utc);
    }
  }
  writeSeparator(writer);

  // Both come from the heartbeat, so both are unknown until one has arrived.
  // Writing 0 for "not armed" before anyone has said so would be a claim.
  writeInteger(writer, snapshot.armed ? 1 : 0, snapshot.haveHeartbeat);
  writeSeparator(writer);
  writeInteger(writer, snapshot.mode, snapshot.haveHeartbeat);
  writeSeparator(writer);

  writeInteger(writer, snapshot.gpsFix, snapshot.gpsFix != UNKNOWN_I8);
  writeSeparator(writer);
  writeInteger(writer, snapshot.sats, snapshot.sats != UNKNOWN_I8);
  writeSeparator(writer);
  writeReal(writer, snapshot.hdop, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.lat, 7);
  writeSeparator(writer);
  writeReal(writer, snapshot.lon, 7);
  writeSeparator(writer);
  writeReal(writer, snapshot.altMsl, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.altRel, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.groundSpeed, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.airSpeed, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.climb, 2);
  writeSeparator(writer);
  writeReal(writer, snapshot.heading, 1);
  writeSeparator(writer);
  writeReal(writer, snapshot.cog, 1);
  writeSeparator(writer);
  writeReal(writer, snapshot.roll, 1);
  writeSeparator(writer);
  writeReal(writer, snapshot.pitch, 1);
  writeSeparator(writer);
  writeReal(writer, snapshot.yaw, 1);
  writeSeparator(writer);
  writeInteger(writer, snapshot.throttle, snapshot.throttle != UNKNOWN_I16);
  writeSeparator(writer);

  writeReal(writer, snapshot.batteryVoltage, 3);
  writeSeparator(writer);
  writeReal(writer, snapshot.batteryCurrent, 2);
  writeSeparator(writer);
  writeInteger(writer, snapshot.batteryRemaining, snapshot.batteryRemaining != UNKNOWN_I16);
  writeSeparator(writer);
  writeInteger(writer, snapshot.consumedMah, snapshot.consumedMah != UNKNOWN_I32);
  writeSeparator(writer);

  writeInteger(writer, snapshot.rcRssi, snapshot.rcRssi != UNKNOWN_I16);
  writeSeparator(writer);
  writeInteger(writer, snapshot.wifiRssi, snapshot.wifiRssi != UNKNOWN_I16);
  writeSeparator(writer);

  const uint32_t linkAge = snapshotLinkAgeMs(snapshot, nowMs);
  writeInteger(writer, linkAge, linkAge != UINT32_MAX);

  writeText(writer, "\n");

  if (!writer.ok) {
    out[0] = 0;
    return 0;
  }
  return writer.length;
}

uint32_t csvRowIndex(const char *line, size_t length) {
  if (line == nullptr || length == 0) return UINT32_MAX;

  uint32_t value = 0;
  size_t digits = 0;
  while (digits < length && line[digits] >= '0' && line[digits] <= '9') {
    value = value * 10 + (uint32_t)(line[digits] - '0');
    digits++;
  }

  if (digits == 0) return UINT32_MAX;
  // A bare number with no separator after it is a truncated line, not a row.
  if (digits < length && line[digits] != ',') return UINT32_MAX;
  return value;
}

size_t csvRowToJson(const char *line, size_t length, char *out, size_t max) {
  if (line == nullptr || out == nullptr || max == 0) return 0;

  // A trailing newline belongs to the file, not to the record.
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) length--;
  if (length == 0) return 0;

  Writer writer = {out, max, 0, true};
  out[0] = 0;

  writeText(writer, "{");

  size_t cursor = 0;
  size_t column = 0;

  while (column < csvColumnCount) {
    const size_t fieldStart = cursor;
    while (cursor < length && line[cursor] != ',') cursor++;
    const size_t fieldLength = cursor - fieldStart;

    // A value carrying a quote or a backslash would need escaping, and no
    // column this firmware writes can contain one. Rather than grow an escaper
    // that is never exercised, the row is rejected: something upstream is wrong
    // and sending it anyway would only move the problem into the database.
    for (size_t i = 0; i < fieldLength; i++) {
      const char character = line[fieldStart + i];
      if (character == '"' || character == '\\') return 0;
    }

    if (column > 0) writeSeparator(writer);
    writeText(writer, "\"");
    writeText(writer, csvColumns[column].name);
    writeText(writer, "\":");

    if (fieldLength == 0) {
      // An empty cell means nobody reported it. null, not 0 - which is the
      // whole reason the cell was left empty in the first place.
      writeText(writer, "null");
    } else if (csvColumns[column].isString) {
      writeText(writer, "\"");
      writeSpan(writer, line + fieldStart, fieldLength);
      writeText(writer, "\"");
    } else {
      writeSpan(writer, line + fieldStart, fieldLength);
    }

    column++;

    if (cursor >= length) break;
    cursor++;  // step over the comma
  }

  // A row with the wrong number of columns was written by a different version
  // of this firmware. Refusing it keeps the mismatch a visible upload failure
  // rather than a set of silently shifted values in the database.
  if (column != csvColumnCount) return 0;

  writeText(writer, "}");

  if (!writer.ok) {
    out[0] = 0;
    return 0;
  }
  return writer.length;
}
