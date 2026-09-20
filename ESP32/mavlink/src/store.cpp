#include "store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "log.h"

// ------------------------------------------------------------ the real disk

class LittleFsPort : public FileSystemPort {
 public:
  bool exists(const char *path) override { return LittleFS.exists(path); }

  uint32_t size(const char *path) override {
    File file = LittleFS.open(path, FILE_READ);
    if (!file) return 0;
    const uint32_t length = file.size();
    file.close();
    return length;
  }

  bool writeAll(const char *path, const char *data, uint32_t length) override {
    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) return false;
    const size_t written = file.write((const uint8_t *)data, length);
    file.close();
    return written == length;
  }

  bool append(const char *path, const char *data, uint32_t length) override {
    File file = LittleFS.open(path, FILE_APPEND);
    if (!file) return false;
    const size_t written = file.write((const uint8_t *)data, length);
    file.close();
    return written == length;
  }

  uint32_t read(const char *path, uint32_t offset, char *out, uint32_t max) override {
    File file = LittleFS.open(path, FILE_READ);
    if (!file) return 0;
    if (offset > 0 && !file.seek(offset)) {
      file.close();
      return 0;
    }
    const int read = file.read((uint8_t *)out, max);
    file.close();
    return read > 0 ? (uint32_t)read : 0;
  }

  bool remove(const char *path) override { return LittleFS.remove(path); }

  uint32_t list(const char *directory, DirEntry *out, uint32_t max) override {
    File folder = LittleFS.open(directory);
    if (!folder || !folder.isDirectory()) return 0;

    uint32_t count = 0;
    File entry = folder.openNextFile();
    while (entry && count < max) {
      if (!entry.isDirectory()) {
        // openNextFile gives the full path on this core; the flight log wants
        // the bare name, and stripping it here keeps that detail out of the
        // part that has host tests.
        const char *name = entry.name();
        const char *slash = strrchr(name, '/');
        if (slash != nullptr) name = slash + 1;

        strncpy(out[count].name, name, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = 0;
        out[count].size = entry.size();
        count++;
      }
      entry.close();
      entry = folder.openNextFile();
    }
    folder.close();
    return count;
  }

  uint32_t totalBytes() override { return LittleFS.totalBytes(); }
  uint32_t usedBytes() override { return LittleFS.usedBytes(); }
};

// ---------------------------------------------------------------- the state

static LittleFsPort _port;
static FlightLog _flightLog;
static TelemetrySnapshot _snapshot;
static SemaphoreHandle_t _snapshotMutex = nullptr;
static SemaphoreHandle_t _logMutex = nullptr;

bool storeBegin() {
  _snapshotMutex = xSemaphoreCreateMutex();
  _logMutex = xSemaphoreCreateMutex();

  snapshotReset(_snapshot);

  // Formats on first boot or after a partition table change. A board whose
  // filesystem will not mount is a board that records nothing, so formatting is
  // the right default even though it discards whatever was there - which, by
  // definition, could not be read anyway.
  if (!LittleFS.begin(true)) {
    LOG_ERROR("fs", "mount failed");
    return false;
  }

  if (!LittleFS.exists("/f")) LittleFS.mkdir("/f");

  if (!_flightLog.begin(&_port, FsReserveBytes)) {
    LOG_ERROR("fs", "flight log init failed");
    return false;
  }

  LOG_INFO("fs", "mounted %lu/%lu bytes used", (unsigned long)LittleFS.usedBytes(),
           (unsigned long)LittleFS.totalBytes());
  return true;
}

void storeApplyMessage(const void *message, uint32_t nowMs) {
  if (xSemaphoreTake(_snapshotMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  snapshotApply(_snapshot, message, nowMs);
  xSemaphoreGive(_snapshotMutex);
}

void storeSnapshot(TelemetrySnapshot &out) {
  if (xSemaphoreTake(_snapshotMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    // Copied without the lock rather than not copied at all. The worst case is
    // one row mixing values from either side of an update, which is a row that
    // is very slightly wrong; the alternative is no row, which is a hole in the
    // file and indistinguishable from the board having been switched off.
    out = _snapshot;
    return;
  }
  out = _snapshot;
  xSemaphoreGive(_snapshotMutex);
}

void storeSetWifiRssi(int16_t rssi) {
  if (xSemaphoreTake(_snapshotMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  _snapshot.wifiRssi = rssi;
  xSemaphoreGive(_snapshotMutex);
}

bool storeLockLog(uint32_t timeoutMs) {
  return xSemaphoreTake(_logMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void storeUnlockLog() { xSemaphoreGive(_logMutex); }

FlightLog &storeFlightLog() { return _flightLog; }

uint32_t storeFsTotalBytes() { return LittleFS.totalBytes(); }
uint32_t storeFsUsedBytes() { return LittleFS.usedBytes(); }
