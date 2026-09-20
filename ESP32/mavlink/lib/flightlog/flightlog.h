#pragma once

#include <stddef.h>
#include <stdint.h>

// The flight log: a numbered CSV file per flight, plus how much of each one has
// already reached the service.
//
// Everything here is written against FileSystemPort rather than LittleFS, for
// one reason that matters: the interesting behaviour is what happens when the
// disk is nearly full, and that is intolerable to test on a board. The host
// tests run this against an in-memory filesystem they can fill to the byte.
//
// Two files per flight:
//
//   /f/<id>.csv    the header and the rows
//   /f/<id>.mta    one line, "uploaded=<byte offset>"
//
// The meta file holds nothing else on purpose. The row index is the first CSV
// column, so the uploader reads it out of the data; a second copy kept
// alongside is a second copy that can disagree.

static const uint32_t FlightsMax = 48;

struct FlightInfo {
  uint16_t id;
  uint32_t bytes;
  // How many bytes of this flight the service has acknowledged. Equal to
  // `bytes` means fully uploaded, which is what makes a flight safe to delete.
  uint32_t uploaded;
};

struct DirEntry {
  char name[24];
  uint32_t size;
};

// The only thing between this module and a real filesystem. Small on purpose:
// every method here has to be implemented twice, once for LittleFS and once for
// the fake, and a wide port makes the fake drift from the real one.
class FileSystemPort {
 public:
  virtual ~FileSystemPort() {}

  virtual bool exists(const char *path) = 0;
  virtual uint32_t size(const char *path) = 0;
  virtual bool writeAll(const char *path, const char *data, uint32_t length) = 0;
  virtual bool append(const char *path, const char *data, uint32_t length) = 0;
  virtual uint32_t read(const char *path, uint32_t offset, char *out, uint32_t max) = 0;
  virtual bool remove(const char *path) = 0;
  virtual uint32_t list(const char *directory, DirEntry *out, uint32_t max) = 0;
  virtual uint32_t totalBytes() = 0;
  virtual uint32_t usedBytes() = 0;
};

// The on-disk path of a flight's CSV. Public because the board's web server
// hands the file straight to the browser rather than reading it through this
// class - a download is bytes, not rows - and the naming should still live in
// exactly one place.
void flightLogCsvPath(uint16_t id, char *out, size_t max);

class FlightLog {
 public:
  // `reserveBytes` is the free space never knowingly consumed. LittleFS needs
  // room to manoeuvre and a filesystem run to the last byte fails its writes in
  // ways that are much harder to recover from than a deleted old flight.
  bool begin(FileSystemPort *filesystem, uint32_t reserveBytes);

  // Starts a new flight, writing the CSV header. Returns false if the file
  // could not be created - the caller keeps running either way, because a board
  // that cannot write is still a board that can serve what it wrote last time.
  bool openFlight(uint16_t id, const char *header);

  // Appends one rendered row, making room first if the filesystem is close to
  // full. Returns false when there is no open flight or the write failed.
  bool appendRow(const char *row, uint32_t length);

  uint16_t currentFlightId() const { return _currentId; }
  bool hasOpenFlight() const { return _currentId != 0; }

  // Rows appended to the open flight so far, which is the index the next row
  // will carry.
  uint32_t nextIndex() const { return _nextIndex; }

  // Oldest first. The ordering is the upload order and the deletion order, so
  // it is fixed here rather than left to whatever the directory returns.
  uint32_t listFlights(FlightInfo *out, uint32_t max);

  bool flightInfo(uint16_t id, FlightInfo &out);

  // Reads from `offset`, never splitting a row: the returned length always ends
  // on a newline, so the caller can hand whole rows to the uploader without
  // reassembling anything. Returns 0 at end of file.
  uint32_t readChunk(uint16_t id, uint32_t offset, char *out, uint32_t max);

  // Records how much of a flight the service has acknowledged. Never moves
  // backwards on its own - a service reporting a lower offset is handled by the
  // caller, deliberately, because rewinding is a decision and not a detail.
  bool setUploaded(uint16_t id, uint32_t offset);

  bool removeFlight(uint16_t id);

  uint32_t freeBytes();

  // Deletes the oldest flight that has been fully uploaded. If none has, and
  // `force` is set, deletes the oldest flight regardless. Returns the id it
  // deleted, or 0.
  //
  // Losing the oldest rows beats refusing to record the flight that is
  // happening now, which is why `force` exists at all.
  uint16_t dropOldest(bool force);

 private:
  bool metaPath(uint16_t id, char *out, size_t max) const;
  bool csvPath(uint16_t id, char *out, size_t max) const;
  uint32_t readUploaded(uint16_t id);
  bool ensureSpace(uint32_t wanted);

  FileSystemPort *_filesystem = nullptr;
  uint32_t _reserveBytes = 0;
  uint16_t _currentId = 0;
  uint32_t _nextIndex = 0;
};
