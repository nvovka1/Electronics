// Host tests for everything in lib/: the message fold, the row renderer, the
// row-to-JSON converter and the flight log. No board, about a second.
//
//   pio test -e native

#include <common/mavlink.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "csv_row.h"
#include "flightlog.h"
#include "snapshot.h"

// ------------------------------------------------------------ the fake disk
// An in-memory FileSystemPort with a size it will actually enforce. The whole
// reason the flight log is written against a port is that its interesting
// behaviour is what happens when the disk is nearly full, and filling a real
// board's flash to the byte to find out is not a test anybody runs twice.

class FakeFs : public FileSystemPort {
 public:
  explicit FakeFs(uint32_t capacity) : _capacity(capacity) {}

  bool exists(const char *path) override { return _files.count(path) > 0; }

  uint32_t size(const char *path) override {
    auto found = _files.find(path);
    return found == _files.end() ? 0 : (uint32_t)found->second.size();
  }

  bool writeAll(const char *path, const char *data, uint32_t length) override {
    const uint32_t existing = size(path);
    if (usedBytes() - existing + length > _capacity) return false;
    _files[path] = std::string(data, length);
    return true;
  }

  bool append(const char *path, const char *data, uint32_t length) override {
    if (usedBytes() + length > _capacity) return false;
    _files[path] += std::string(data, length);
    return true;
  }

  uint32_t read(const char *path, uint32_t offset, char *out, uint32_t max) override {
    auto found = _files.find(path);
    if (found == _files.end() || offset >= found->second.size()) return 0;
    const uint32_t available = (uint32_t)found->second.size() - offset;
    const uint32_t length = available < max ? available : max;
    memcpy(out, found->second.data() + offset, length);
    return length;
  }

  bool remove(const char *path) override { return _files.erase(path) > 0; }

  uint32_t list(const char *directory, const char *suffix, DirEntry *out,
                uint32_t max) override {
    const std::string prefix = std::string(directory) + "/";
    const std::string wanted = suffix == nullptr ? std::string() : std::string(suffix);
    uint32_t count = 0;
    for (const auto &entry : _files) {
      if (count >= max) break;
      if (entry.first.rfind(prefix, 0) != 0) continue;
      const std::string name = entry.first.substr(prefix.size());
      if (!wanted.empty() &&
          (name.size() < wanted.size() ||
           name.compare(name.size() - wanted.size(), wanted.size(), wanted) != 0)) {
        continue;
      }
      strncpy(out[count].name, name.c_str(), sizeof(out[count].name) - 1);
      out[count].name[sizeof(out[count].name) - 1] = 0;
      out[count].size = (uint32_t)entry.second.size();
      count++;
    }
    return count;
  }

  uint32_t totalBytes() override { return _capacity; }

  uint32_t usedBytes() override {
    uint32_t used = 0;
    for (const auto &entry : _files) used += (uint32_t)entry.second.size();
    return used;
  }

  std::string contents(const char *path) { return _files[path]; }

 protected:
  uint32_t _capacity;
  std::map<std::string, std::string> _files;
};

// ------------------------------------------------------------------ helpers

static mavlink_message_t heartbeat(bool armed, uint32_t mode) {
  mavlink_message_t message;
  mavlink_msg_heartbeat_pack(1, MAV_COMP_ID_AUTOPILOT1, &message, MAV_TYPE_QUADROTOR,
                             MAV_AUTOPILOT_ARDUPILOTMEGA,
                             armed ? MAV_MODE_FLAG_SAFETY_ARMED : 0, mode, MAV_STATE_ACTIVE);
  return message;
}

static std::vector<std::string> split(const std::string &text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char character : text) {
    if (character == separator) {
      parts.push_back(current);
      current.clear();
    } else {
      current += character;
    }
  }
  parts.push_back(current);
  return parts;
}

// -------------------------------------------------------------- the columns
// The header and the table are two spellings of the same thing and the service
// holds a third. If these ever disagree, every downstream number is attributed
// to the wrong column - which is the kind of wrong that gets believed.

static void test_header_matches_column_table(void) {
  std::string joined;
  for (size_t i = 0; i < csvColumnCount; i++) {
    if (i > 0) joined += ",";
    joined += csvColumns[i].name;
  }
  TEST_ASSERT_EQUAL_STRING(CSV_HEADER, joined.c_str());
}

static void test_row_has_one_field_per_column(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  char row[CsvRowMax];
  const size_t length = csvRenderRow(0, snapshot, 1000, row, sizeof(row));
  TEST_ASSERT_TRUE(length > 0);

  std::string text(row, length - 1);  // drop the newline
  TEST_ASSERT_EQUAL_UINT32(csvColumnCount, split(text, ',').size());
}

// ------------------------------------------------------------- the snapshot

static void test_unknown_stays_empty(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  char row[CsvRowMax];
  csvRenderRow(7, snapshot, 1234, row, sizeof(row));

  const std::vector<std::string> fields = split(std::string(row), ',');

  TEST_ASSERT_EQUAL_STRING("7", fields[0].c_str());
  TEST_ASSERT_EQUAL_STRING("1234", fields[1].c_str());
  // utc, armed, mode, gpsFix: nobody has said anything yet, so all empty.
  TEST_ASSERT_EQUAL_STRING("", fields[2].c_str());
  TEST_ASSERT_EQUAL_STRING("", fields[3].c_str());
  TEST_ASSERT_EQUAL_STRING("", fields[4].c_str());
  TEST_ASSERT_EQUAL_STRING("", fields[5].c_str());
}

static void test_heartbeat_sets_armed_and_mode(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  const mavlink_message_t message = heartbeat(true, 4);
  snapshotApply(snapshot, &message, 5000);

  TEST_ASSERT_TRUE(snapshot.haveHeartbeat);
  TEST_ASSERT_TRUE(snapshot.armed);
  TEST_ASSERT_EQUAL_UINT32(4, snapshot.mode);
  TEST_ASSERT_EQUAL_UINT32(0, snapshotLinkAgeMs(snapshot, 5000));
  TEST_ASSERT_EQUAL_UINT32(1500, snapshotLinkAgeMs(snapshot, 6500));
}

static void test_heartbeat_from_another_component_is_ignored(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  // A gimbal, a companion computer, or our own heartbeat echoed back. Accepting
  // it would keep the link looking alive after the autopilot had gone quiet -
  // the one thing the link age exists to reveal.
  mavlink_message_t message;
  mavlink_msg_heartbeat_pack(1, MAV_COMP_ID_GIMBAL, &message, MAV_TYPE_GIMBAL,
                             MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
  snapshotApply(snapshot, &message, 5000);

  TEST_ASSERT_FALSE(snapshot.haveHeartbeat);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, snapshotLinkAgeMs(snapshot, 5000));
}

static void test_battery_unknowns_are_not_zero(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t message;
  // Every field set to MAVLink's own "no data" marker, which is the largest
  // value it can hold - a naive reader sees 65.535 V and 655 A.
  mavlink_msg_sys_status_pack(1, MAV_COMP_ID_AUTOPILOT1, &message, /*present*/ 0,
                              /*enabled*/ 0, /*health*/ 0, /*load*/ 0,
                              /*voltage*/ UINT16_MAX, /*current*/ -1, /*remaining*/ -1,
                              /*dropRate*/ 0, /*errorsComm*/ 0, 0, 0, 0, 0,
                              /*presentExt*/ 0, /*enabledExt*/ 0, /*healthExt*/ 0);
  snapshotApply(snapshot, &message, 1000);

  TEST_ASSERT_TRUE(isnan(snapshot.batteryVoltage));
  TEST_ASSERT_TRUE(isnan(snapshot.batteryCurrent));
  TEST_ASSERT_EQUAL_INT16(UNKNOWN_I16, snapshot.batteryRemaining);
}

static void test_battery_values_are_scaled(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t message;
  mavlink_msg_sys_status_pack(1, MAV_COMP_ID_AUTOPILOT1, &message, /*present*/ 0,
                              /*enabled*/ 0, /*health*/ 0, /*load*/ 0,
                              /*voltage*/ 16800, /*current*/ 1250, /*remaining*/ 74,
                              /*dropRate*/ 0, /*errorsComm*/ 0, 0, 0, 0, 0,
                              /*presentExt*/ 0, /*enabledExt*/ 0, /*healthExt*/ 0);
  snapshotApply(snapshot, &message, 1000);

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 16.8f, snapshot.batteryVoltage);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, snapshot.batteryCurrent);
  TEST_ASSERT_EQUAL_INT16(74, snapshot.batteryRemaining);
}

static void test_global_position_wins_over_raw_gps(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t filtered;
  mavlink_msg_global_position_int_pack(1, MAV_COMP_ID_AUTOPILOT1, &filtered, 1000, 501234567,
                                       -1234567, 120000, 35000, 0, 0, 0, 9000);
  snapshotApply(snapshot, &filtered, 10000);

  mavlink_message_t raw;
  mavlink_msg_gps_raw_int_pack(1, MAV_COMP_ID_AUTOPILOT1, &raw, 1000, 3, 999999999, 999999999,
                               99000, 150, UINT16_MAX, 0, 12000, 11, 0, 0, 0, 0, 0, 0);
  snapshotApply(snapshot, &raw, 10100);

  // The raw fix count and HDOP are taken; its position is not, because the
  // filtered estimate is still arriving.
  TEST_ASSERT_EQUAL_INT8(3, snapshot.gpsFix);
  TEST_ASSERT_EQUAL_INT8(11, snapshot.sats);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, snapshot.hdop);
  TEST_ASSERT_DOUBLE_WITHIN(0.0000001, 50.1234567, snapshot.lat);
}

static void test_raw_gps_fills_in_when_the_estimate_goes_quiet(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t filtered;
  mavlink_msg_global_position_int_pack(1, MAV_COMP_ID_AUTOPILOT1, &filtered, 1000, 501234567,
                                       -1234567, 120000, 35000, 0, 0, 0, 9000);
  snapshotApply(snapshot, &filtered, 10000);

  // Four seconds later with nothing from the EKF in between: an aircraft whose
  // position estimate has given up while its GPS is still reporting, which is
  // exactly the case worth recording.
  mavlink_message_t raw;
  mavlink_msg_gps_raw_int_pack(1, MAV_COMP_ID_AUTOPILOT1, &raw, 1000, 3, 521234567, -1234567,
                               99000, 150, UINT16_MAX, 0, 12000, 11, 0, 0, 0, 0, 0, 0);
  snapshotApply(snapshot, &raw, 14000);

  TEST_ASSERT_DOUBLE_WITHIN(0.0000001, 52.1234567, snapshot.lat);
}

static void test_utc_comes_from_the_flight_controller(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t message;
  // 2026-09-20T12:00:00.000Z
  mavlink_msg_system_time_pack(1, MAV_COMP_ID_AUTOPILOT1, &message, 1789905600000000ULL, 1000);
  snapshotApply(snapshot, &message, 20000);

  char row[CsvRowMax];
  csvRenderRow(0, snapshot, 20500, row, sizeof(row));

  // Half a second after the message arrived, so half a second past the stamp.
  const std::vector<std::string> fields = split(std::string(row), ',');
  TEST_ASSERT_EQUAL_STRING("2026-09-20T12:00:00.500Z", fields[2].c_str());
}

static void test_zero_system_time_is_rejected(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  // What ArduPilot sends before the GPS has a fix. Accepting it would stamp the
  // whole flight as 1970, which sorts wrongly for ever after.
  mavlink_message_t message;
  mavlink_msg_system_time_pack(1, MAV_COMP_ID_AUTOPILOT1, &message, 0, 1000);
  snapshotApply(snapshot, &message, 20000);

  TEST_ASSERT_EQUAL_UINT64(0, snapshot.unixTimeUs);
}

// ------------------------------------------------------------------ to JSON

static void test_row_to_json_keeps_unknowns_null(void) {
  const char *line = "3,1500,,,,,,,,,,,,,,,,,,,,,,,,,,\n";
  char json[1024];
  const size_t length = csvRowToJson(line, strlen(line), json, sizeof(json));

  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"index\":3"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"tMs\":1500"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"utc\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"linkAgeMs\":null"));
  TEST_ASSERT_NULL(strstr(json, "\"utc\":0"));
}

static void test_row_to_json_quotes_only_the_string_column(void) {
  TelemetrySnapshot snapshot;
  snapshotReset(snapshot);

  mavlink_message_t time;
  mavlink_msg_system_time_pack(1, MAV_COMP_ID_AUTOPILOT1, &time, 1789905600000000ULL, 1000);
  snapshotApply(snapshot, &time, 1000);

  const mavlink_message_t beat = heartbeat(false, 0);
  snapshotApply(snapshot, &beat, 1000);

  char row[CsvRowMax];
  const size_t rowLength = csvRenderRow(12, snapshot, 1000, row, sizeof(row));

  char json[1024];
  TEST_ASSERT_TRUE(csvRowToJson(row, rowLength, json, sizeof(json)) > 0);

  TEST_ASSERT_NOT_NULL(strstr(json, "\"utc\":\"2026-09-20T12:00:00.000Z\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"armed\":0"));
  TEST_ASSERT_NULL(strstr(json, "\"armed\":\"0\""));
}

static void test_short_row_is_refused(void) {
  // A row written by a different version of this firmware. Refusing it keeps
  // the mismatch a visible upload failure rather than shifted values.
  const char *line = "3,1500,,\n";
  char json[1024];
  TEST_ASSERT_EQUAL_UINT32(0, csvRowToJson(line, strlen(line), json, sizeof(json)));
}

static void test_row_index_is_read_from_the_line(void) {
  TEST_ASSERT_EQUAL_UINT32(42, csvRowIndex("42,1,2,3", 8));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, csvRowIndex(CSV_HEADER, strlen(CSV_HEADER)));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, csvRowIndex("", 0));
}

// -------------------------------------------------------------- flight log

static void test_flight_starts_with_the_header(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  TEST_ASSERT_TRUE(flightLog.begin(&filesystem, 1000, FlightsMax));
  TEST_ASSERT_TRUE(flightLog.openFlight(1, CSV_HEADER));

  const std::string contents = filesystem.contents("/f/1.csv");
  TEST_ASSERT_EQUAL_STRING((std::string(CSV_HEADER) + "\n").c_str(), contents.c_str());

  // The header is uploaded from the start: the service never wanted it, and a
  // cursor left at zero would make every flight look permanently un-uploaded
  // and therefore undeletable.
  FlightInfo info;
  TEST_ASSERT_TRUE(flightLog.flightInfo(1, info));
  TEST_ASSERT_EQUAL_UINT32(info.bytes, info.uploaded);
}

static void test_rows_are_appended_and_indexed(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);
  flightLog.openFlight(1, CSV_HEADER);

  TEST_ASSERT_EQUAL_UINT32(0, flightLog.nextIndex());
  TEST_ASSERT_TRUE(flightLog.appendRow("0,10\n", 5));
  TEST_ASSERT_EQUAL_UINT32(1, flightLog.nextIndex());
  TEST_ASSERT_TRUE(flightLog.appendRow("1,20\n", 5));
  TEST_ASSERT_EQUAL_UINT32(2, flightLog.nextIndex());
}

static void test_read_chunk_never_splits_a_row(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);
  flightLog.openFlight(1, CSV_HEADER);

  flightLog.appendRow("0,aaaa\n", 7);
  flightLog.appendRow("1,bbbb\n", 7);

  const uint32_t headerEnd = (uint32_t)strlen(CSV_HEADER) + 1;

  char buffer[64];
  // Ten bytes is a row and a half. The half is not returned: handing it to the
  // uploader would have it rejected for ever, because the cursor never advances
  // past a row that never completes.
  const uint32_t length = flightLog.readChunk(1, headerEnd, buffer, 11);
  TEST_ASSERT_EQUAL_UINT32(7, length);
  TEST_ASSERT_EQUAL_STRING("0,aaaa\n", buffer);
}

static void test_uploaded_cursor_survives_a_reopen(void) {
  FakeFs filesystem(100000);

  {
    FlightLog flightLog;
    flightLog.begin(&filesystem, 1000, FlightsMax);
    flightLog.openFlight(1, CSV_HEADER);
    flightLog.appendRow("0,aaaa\n", 7);
    TEST_ASSERT_TRUE(flightLog.setUploaded(1, 4242));
  }

  FlightLog reopened;
  reopened.begin(&filesystem, 1000, FlightsMax);

  FlightInfo info;
  TEST_ASSERT_TRUE(reopened.flightInfo(1, info));
  TEST_ASSERT_EQUAL_UINT32(4242, info.uploaded);
}

static void test_a_flight_with_no_meta_file_reads_as_not_uploaded(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);
  flightLog.openFlight(1, CSV_HEADER);
  flightLog.appendRow("0,aaaa\n", 7);

  // A flight recorded by an older build, or one whose meta was lost. Ordinary,
  // not an error: it reads as nothing uploaded, which is the safe answer -
  // the rows get sent again rather than skipped.
  TEST_ASSERT_TRUE(filesystem.remove("/f/1.mta"));

  FlightInfo info;
  TEST_ASSERT_TRUE(flightLog.flightInfo(1, info));
  TEST_ASSERT_EQUAL_UINT32(0, info.uploaded);
  TEST_ASSERT_TRUE(info.bytes > 0);
}

static void test_flights_come_back_oldest_first(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);

  flightLog.openFlight(9, CSV_HEADER);
  flightLog.openFlight(3, CSV_HEADER);
  flightLog.openFlight(11, CSV_HEADER);

  FlightInfo flights[FlightsMax];
  const uint32_t count = flightLog.listFlights(flights, FlightsMax);

  TEST_ASSERT_EQUAL_UINT32(3, count);
  TEST_ASSERT_EQUAL_UINT16(3, flights[0].id);
  TEST_ASSERT_EQUAL_UINT16(9, flights[1].id);
  TEST_ASSERT_EQUAL_UINT16(11, flights[2].id);
}

static void test_uploaded_flights_are_dropped_before_unuploaded_ones(void) {
  FakeFs filesystem(100000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);

  flightLog.openFlight(1, CSV_HEADER);
  flightLog.appendRow("0,aaaa\n", 7);
  // Flight 1 is the older one but still has rows the service has not seen.
  flightLog.setUploaded(1, 0);

  flightLog.openFlight(2, CSV_HEADER);  // fully uploaded by definition

  // Flight 2 is open, so it is protected; flight 1 is not fully uploaded, so
  // nothing can be dropped without being told to.
  TEST_ASSERT_EQUAL_UINT16(0, flightLog.dropOldest(false));

  TEST_ASSERT_EQUAL_UINT16(1, flightLog.dropOldest(true));
  TEST_ASSERT_FALSE(filesystem.exists("/f/1.csv"));
  TEST_ASSERT_TRUE(filesystem.exists("/f/2.csv"));
}

static void test_a_full_disk_drops_history_rather_than_the_current_flight(void) {
  // Room for the two headers, one old flight's rows, and very little else.
  const uint32_t headerBytes = (uint32_t)strlen(CSV_HEADER) + 1;
  FakeFs filesystem(headerBytes * 2 + 220);

  FlightLog flightLog;
  flightLog.begin(&filesystem, 32, FlightsMax);

  flightLog.openFlight(1, CSV_HEADER);
  for (int i = 0; i < 10; i++) flightLog.appendRow("0,aaaaaaaaa\n", 12);
  flightLog.setUploaded(1, 100000);  // the service has everything

  flightLog.openFlight(2, CSV_HEADER);

  // Enough rows to need the space flight 1 is holding.
  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_TRUE(flightLog.appendRow("0,aaaaaaaaa\n", 12));
  }

  TEST_ASSERT_FALSE(filesystem.exists("/f/1.csv"));
  TEST_ASSERT_TRUE(filesystem.exists("/f/2.csv"));
  TEST_ASSERT_EQUAL_UINT32(10, flightLog.nextIndex());
}

static void test_a_listing_is_not_halved_by_the_meta_files(void) {
  FakeFs filesystem(1000000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);

  // Two files per flight. Before the listing filtered by suffix, FlightsMax
  // entries covered only half this many flights and the rest became invisible:
  // never uploaded, never deleted, holding their space for ever.
  for (uint16_t id = 1; id <= 40; id++) flightLog.openFlight(id, CSV_HEADER);

  FlightInfo flights[FlightsMax];
  TEST_ASSERT_EQUAL_UINT32(40, flightLog.listFlights(flights, FlightsMax));
}

static void test_the_board_keeps_only_so_many_flights(void) {
  FakeFs filesystem(1000000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, 5);

  for (uint16_t id = 1; id <= 12; id++) flightLog.openFlight(id, CSV_HEADER);

  FlightInfo flights[FlightsMax];
  const uint32_t count = flightLog.listFlights(flights, FlightsMax);

  // Every power-on starts a flight, so without this an afternoon of switching
  // on and off fills the directory with tiny files.
  TEST_ASSERT_EQUAL_UINT32(5, count);
  TEST_ASSERT_EQUAL_UINT16(12, flights[count - 1].id);
}

static void test_an_un_uploaded_flight_is_never_pruned_by_count(void) {
  FakeFs filesystem(1000000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, 2);

  flightLog.openFlight(1, CSV_HEADER);
  flightLog.appendRow("0,aaaa", 6);
  flightLog.setUploaded(1, 0);

  for (uint16_t id = 2; id <= 8; id++) flightLog.openFlight(id, CSV_HEADER);

  // It is over the limit and it is the oldest, and it still stays: the service
  // has never acknowledged it, so it exists nowhere else.
  TEST_ASSERT_TRUE(filesystem.exists("/f/1.csv"));
}

static void test_a_meta_file_outlives_a_csv_that_could_not_be_deleted(void) {
  // A port that refuses the delete - a real one can, and the old code removed
  // the meta anyway, leaving a flight that read as never uploaded, re-sent
  // itself in full and could never be deleted for space.
  class StubbornFs : public FakeFs {
   public:
    using FakeFs::FakeFs;
    bool remove(const char *path) override {
      const std::string name(path);
      if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".csv") == 0) return false;
      return FakeFs::remove(path);
    }
  };

  StubbornFs filesystem(1000000);
  FlightLog flightLog;
  flightLog.begin(&filesystem, 1000, FlightsMax);
  flightLog.openFlight(1, CSV_HEADER);
  flightLog.openFlight(2, CSV_HEADER);

  TEST_ASSERT_FALSE(flightLog.removeFlight(1));
  TEST_ASSERT_TRUE(filesystem.exists("/f/1.mta"));
}

// -------------------------------------------------------------------- setup

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_header_matches_column_table);
  RUN_TEST(test_row_has_one_field_per_column);

  RUN_TEST(test_unknown_stays_empty);
  RUN_TEST(test_heartbeat_sets_armed_and_mode);
  RUN_TEST(test_heartbeat_from_another_component_is_ignored);
  RUN_TEST(test_battery_unknowns_are_not_zero);
  RUN_TEST(test_battery_values_are_scaled);
  RUN_TEST(test_global_position_wins_over_raw_gps);
  RUN_TEST(test_raw_gps_fills_in_when_the_estimate_goes_quiet);
  RUN_TEST(test_utc_comes_from_the_flight_controller);
  RUN_TEST(test_zero_system_time_is_rejected);

  RUN_TEST(test_row_to_json_keeps_unknowns_null);
  RUN_TEST(test_row_to_json_quotes_only_the_string_column);
  RUN_TEST(test_short_row_is_refused);
  RUN_TEST(test_row_index_is_read_from_the_line);

  RUN_TEST(test_flight_starts_with_the_header);
  RUN_TEST(test_rows_are_appended_and_indexed);
  RUN_TEST(test_read_chunk_never_splits_a_row);
  RUN_TEST(test_uploaded_cursor_survives_a_reopen);
  RUN_TEST(test_a_flight_with_no_meta_file_reads_as_not_uploaded);
  RUN_TEST(test_flights_come_back_oldest_first);
  RUN_TEST(test_uploaded_flights_are_dropped_before_unuploaded_ones);
  RUN_TEST(test_a_full_disk_drops_history_rather_than_the_current_flight);
  RUN_TEST(test_a_listing_is_not_halved_by_the_meta_files);
  RUN_TEST(test_the_board_keeps_only_so_many_flights);
  RUN_TEST(test_an_un_uploaded_flight_is_never_pruned_by_count);
  RUN_TEST(test_a_meta_file_outlives_a_csv_that_could_not_be_deleted);

  return UNITY_END();
}
