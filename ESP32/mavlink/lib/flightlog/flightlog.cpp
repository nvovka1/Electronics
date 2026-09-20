#include "flightlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FlightDirectory = "/f";

void flightLogCsvPath(uint16_t id, char *out, size_t max) {
  snprintf(out, max, "%s/%u.csv", FlightDirectory, (unsigned)id);
}

bool FlightLog::csvPath(uint16_t id, char *out, size_t max) const {
  flightLogCsvPath(id, out, max);
  return out[0] != 0;
}

bool FlightLog::metaPath(uint16_t id, char *out, size_t max) const {
  return snprintf(out, max, "%s/%u.mta", FlightDirectory, (unsigned)id) > 0;
}

bool FlightLog::begin(FileSystemPort *filesystem, uint32_t reserveBytes) {
  _filesystem = filesystem;
  _reserveBytes = reserveBytes;
  _currentId = 0;
  _nextIndex = 0;
  return _filesystem != nullptr;
}

uint32_t FlightLog::freeBytes() {
  if (_filesystem == nullptr) return 0;
  const uint32_t total = _filesystem->totalBytes();
  const uint32_t used = _filesystem->usedBytes();
  return used >= total ? 0 : total - used;
}

uint32_t FlightLog::readUploaded(uint16_t id) {
  char path[32];
  if (!metaPath(id, path, sizeof(path))) return 0;

  char buffer[48];
  const uint32_t read = _filesystem->read(path, 0, buffer, sizeof(buffer) - 1);
  if (read == 0) return 0;
  buffer[read] = 0;

  const char *marker = strstr(buffer, "uploaded=");
  if (marker == nullptr) return 0;
  return (uint32_t)strtoul(marker + 9, nullptr, 10);
}

bool FlightLog::setUploaded(uint16_t id, uint32_t offset) {
  if (_filesystem == nullptr) return false;

  char path[32];
  if (!metaPath(id, path, sizeof(path))) return false;

  char buffer[48];
  const int written = snprintf(buffer, sizeof(buffer), "uploaded=%lu\n", (unsigned long)offset);
  if (written <= 0) return false;

  return _filesystem->writeAll(path, buffer, (uint32_t)written);
}

uint32_t FlightLog::listFlights(FlightInfo *out, uint32_t max) {
  if (_filesystem == nullptr || out == nullptr || max == 0) return 0;

  DirEntry entries[FlightsMax];
  const uint32_t found = _filesystem->list(FlightDirectory, entries, FlightsMax);

  uint32_t count = 0;
  for (uint32_t i = 0; i < found && count < max; i++) {
    const char *name = entries[i].name;
    const char *dot = strrchr(name, '.');
    if (dot == nullptr || strcmp(dot, ".csv") != 0) continue;

    char *end = nullptr;
    const unsigned long id = strtoul(name, &end, 10);
    if (end != dot || id == 0 || id > UINT16_MAX) continue;

    out[count].id = (uint16_t)id;
    out[count].bytes = entries[i].size;
    out[count].uploaded = readUploaded((uint16_t)id);
    count++;
  }

  // Insertion sort by id. Oldest first, because that is both the order they
  // should be uploaded in and the order they should be deleted in, and leaving
  // it to the directory would make both depend on the filesystem's mood.
  for (uint32_t i = 1; i < count; i++) {
    const FlightInfo pending = out[i];
    uint32_t j = i;
    while (j > 0 && out[j - 1].id > pending.id) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = pending;
  }

  return count;
}

bool FlightLog::flightInfo(uint16_t id, FlightInfo &out) {
  if (_filesystem == nullptr) return false;

  char path[32];
  if (!csvPath(id, path, sizeof(path))) return false;
  if (!_filesystem->exists(path)) return false;

  out.id = id;
  out.bytes = _filesystem->size(path);
  out.uploaded = readUploaded(id);
  return true;
}

uint16_t FlightLog::dropOldest(bool force) {
  FlightInfo flights[FlightsMax];
  const uint32_t count = listFlights(flights, FlightsMax);

  for (uint32_t i = 0; i < count; i++) {
    // Never the flight being written. It is the only one that cannot be
    // recovered by waiting.
    if (flights[i].id == _currentId) continue;
    if (flights[i].uploaded >= flights[i].bytes) {
      if (removeFlight(flights[i].id)) return flights[i].id;
    }
  }

  if (!force) return 0;

  for (uint32_t i = 0; i < count; i++) {
    if (flights[i].id == _currentId) continue;
    if (removeFlight(flights[i].id)) return flights[i].id;
  }

  return 0;
}

bool FlightLog::removeFlight(uint16_t id) {
  if (_filesystem == nullptr) return false;

  char csv[32];
  char meta[32];
  if (!csvPath(id, csv, sizeof(csv)) || !metaPath(id, meta, sizeof(meta))) return false;

  const bool removed = _filesystem->remove(csv);
  // The meta file may legitimately not exist - a flight that was never uploaded
  // still has rows worth keeping - so its removal is not part of the verdict.
  _filesystem->remove(meta);

  if (removed && id == _currentId) {
    _currentId = 0;
    _nextIndex = 0;
  }
  return removed;
}

bool FlightLog::ensureSpace(uint32_t wanted) {
  if (_filesystem == nullptr) return false;

  // Two passes at most per row. Deleting in a loop until the space appears
  // could empty the whole log to make room for one row, which would turn a
  // full disk into no history at all.
  for (int attempt = 0; attempt < 2; attempt++) {
    if (freeBytes() >= _reserveBytes + wanted) return true;
    if (dropOldest(attempt == 1) == 0) break;
  }

  return freeBytes() >= wanted;
}

bool FlightLog::openFlight(uint16_t id, const char *header) {
  if (_filesystem == nullptr || id == 0 || header == nullptr) return false;

  const uint32_t headerLength = (uint32_t)strlen(header);
  ensureSpace(headerLength + 1024);

  char path[32];
  if (!csvPath(id, path, sizeof(path))) return false;

  char buffer[512];
  const int written = snprintf(buffer, sizeof(buffer), "%s\n", header);
  if (written <= 0) return false;

  if (!_filesystem->writeAll(path, buffer, (uint32_t)written)) return false;

  // The header counts as uploaded from the start. It is not a row, the service
  // does not want it, and leaving the cursor at zero would make every flight
  // look permanently un-uploaded and therefore undeletable.
  setUploaded(id, (uint32_t)written);

  _currentId = id;
  _nextIndex = 0;
  return true;
}

bool FlightLog::appendRow(const char *row, uint32_t length) {
  if (_filesystem == nullptr || _currentId == 0 || row == nullptr || length == 0) return false;

  if (!ensureSpace(length)) return false;

  char path[32];
  if (!csvPath(_currentId, path, sizeof(path))) return false;

  if (!_filesystem->append(path, row, length)) return false;

  _nextIndex++;
  return true;
}

uint32_t FlightLog::readChunk(uint16_t id, uint32_t offset, char *out, uint32_t max) {
  if (_filesystem == nullptr || out == nullptr || max < 2) return 0;

  char path[32];
  if (!csvPath(id, path, sizeof(path))) return 0;

  const uint32_t read = _filesystem->read(path, offset, out, max - 1);
  if (read == 0) return 0;
  out[read] = 0;

  // Back up to the last complete row. A half row handed to the uploader would
  // be rejected by the JSON converter, which is correct but would then repeat
  // forever: the cursor never advances past a row that never completes.
  uint32_t end = read;
  while (end > 0 && out[end - 1] != '\n') end--;

  if (end == 0) {
    // One row longer than the buffer. Cannot happen with rows this firmware
    // writes, and if it ever does, stopping here beats looping.
    return 0;
  }

  out[end] = 0;
  return end;
}
