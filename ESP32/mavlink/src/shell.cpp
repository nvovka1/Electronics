#include "shell.h"

#include <Arduino.h>

#include "config.h"
#include "csv_row.h"
#include "log.h"
#include "log_task.h"
#include "mav_task.h"
#include "net_task.h"
#include "settings.h"
#include "store.h"

static const char *Help =
    "commands\r\n"
    "  status                    what the board is doing\r\n"
    "  flights                   every flight on disk\r\n"
    "  head <id> [rows]          first rows of a flight\r\n"
    "  tail <id> [rows]          last rows of a flight\r\n"
    "  rm <id>                   delete one flight\r\n"
    "  set serial <name>         board name, reaches the service as the aircraft\r\n"
    "  set wifi <ssid> <pass>    network to join\r\n"
    "  set url <base-url>        service to report to, no trailing slash\r\n"
    "  set key <api-key>         the X-Api-Key value\r\n"
    "  set baud <rate>           flight controller serial baud\r\n"
    "  set rate <hz>             rows per second\r\n"
    "  set upload on|off         pause uploading without stopping recording\r\n"
    "  reset                     settings back to the built-in defaults\r\n"
    "  reboot\r\n";

static void printStatus() {
  TelemetrySnapshot snapshot;
  storeSnapshot(snapshot);

  const uint32_t linkAge = mavLinkAgeMs();

  Serial.printf("serial      %s\r\n", settings.serial);
  Serial.printf("flight      %u, %lu rows written, %lu failed\r\n", (unsigned)logFlightId(),
                (unsigned long)logRowsWritten(), (unsigned long)logWriteFailures());
  Serial.printf("mav         %lu messages, %lu dropped, ", (unsigned long)mavMessagesSeen(),
                (unsigned long)mavParseErrors());

  if (linkAge == UINT32_MAX) {
    Serial.printf("NO HEARTBEAT (check wiring and `set baud`)\r\n");
  } else {
    Serial.printf("heartbeat %.1fs ago\r\n", linkAge / 1000.0);
  }

  Serial.printf("armed       %s\r\n",
                !snapshot.haveHeartbeat ? "unknown" : (snapshot.armed ? "ARMED" : "disarmed"));
  Serial.printf("gps         fix=%d sats=%d\r\n", (int)snapshot.gpsFix, (int)snapshot.sats);

  if (!isnan(snapshot.batteryVoltage)) {
    Serial.printf("battery     %.2f V\r\n", snapshot.batteryVoltage);
  }

  Serial.printf("net         %s, %s, last http %d, %lu rows up\r\n",
                netIsStationConnected() ? "station" : (netIsAccessPoint() ? "ap" : "down"),
                netAddress().c_str(), netLastHttpStatus(), (unsigned long)netRowsUploaded());
  Serial.printf("url         %s\r\n", settings.baseUrl);
  Serial.printf("upload      %s\r\n", settings.uploadEnabled ? "on" : "off");
  Serial.printf("disk        %lu of %lu bytes used\r\n", (unsigned long)storeFsUsedBytes(),
                (unsigned long)storeFsTotalBytes());
  Serial.printf("rate        %lu Hz\r\n", (unsigned long)settings.logRateHz);
}

static void printFlights() {
  if (!storeLockLog(1000)) {
    Serial.printf("flight log busy\r\n");
    return;
  }

  FlightInfo flights[FlightsMax];
  const uint32_t count = storeFlightLog().listFlights(flights, FlightsMax);
  storeUnlockLog();

  if (count == 0) {
    Serial.printf("no flights\r\n");
    return;
  }

  Serial.printf("  id     bytes  uploaded\r\n");
  for (uint32_t i = 0; i < count; i++) {
    Serial.printf("%4u  %8lu  %8lu%s\r\n", (unsigned)flights[i].id,
                  (unsigned long)flights[i].bytes, (unsigned long)flights[i].uploaded,
                  flights[i].uploaded >= flights[i].bytes ? "  done" : "");
  }
}

// Prints from an offset, whole rows only. Used by both head and tail; tail just
// starts further in. Reading through the flight log rather than the filesystem
// keeps the path shape in one place.
static void printRows(uint16_t id, uint32_t offset, uint32_t rows) {
  char buffer[1025];

  if (!storeLockLog(1000)) {
    Serial.printf("flight log busy\r\n");
    return;
  }

  uint32_t printed = 0;
  uint32_t cursor = offset;

  while (printed < rows) {
    const uint32_t length = storeFlightLog().readChunk(id, cursor, buffer, sizeof(buffer));
    if (length == 0) break;
    cursor += length;

    uint32_t start = 0;
    while (start < length && printed < rows) {
      uint32_t end = start;
      while (end < length && buffer[end] != '\n') end++;
      Serial.write((const uint8_t *)(buffer + start), end - start);
      Serial.printf("\r\n");
      printed++;
      start = end + 1;
    }
  }

  storeUnlockLog();

  if (printed == 0) Serial.printf("no such flight, or empty\r\n");
}

static void handleTail(uint16_t id, uint32_t rows) {
  FlightInfo flight;

  if (!storeLockLog(1000)) {
    Serial.printf("flight log busy\r\n");
    return;
  }
  const bool found = storeFlightLog().flightInfo(id, flight);
  storeUnlockLog();

  if (!found) {
    Serial.printf("no such flight\r\n");
    return;
  }

  // A row is about 120 bytes; asking for a little more than needed and letting
  // readChunk trim to the first whole row is simpler than seeking backwards
  // through the file for newlines.
  const uint32_t want = rows * 160;
  const uint32_t offset = flight.bytes > want ? flight.bytes - want : 0;

  printRows(id, offset, rows + 8);
}

static void handleSet(char *arguments) {
  char *what = strtok(arguments, " ");
  if (what == nullptr) {
    Serial.printf("set what?\r\n");
    return;
  }

  if (strcmp(what, "serial") == 0) {
    char *value = strtok(nullptr, " ");
    const bool ok = value != nullptr && settingsSaveSerial(value);
    Serial.printf(ok ? "serial=%s\r\n" : "rejected (letters, digits, - and _ only)\r\n",
                  settings.serial);
    return;
  }

  if (strcmp(what, "wifi") == 0) {
    char *ssid = strtok(nullptr, " ");
    // Everything after the ssid, so a password with spaces works. strtok would
    // cut it at the first one.
    char *password = ssid != nullptr ? strtok(nullptr, "") : nullptr;
    const bool ok = ssid != nullptr && password != nullptr && settingsSaveWifi(ssid, password);
    Serial.printf(ok ? "wifi saved, reboot to join\r\n" : "usage: set wifi <ssid> <pass>\r\n");
    return;
  }

  if (strcmp(what, "url") == 0) {
    char *value = strtok(nullptr, " ");
    const bool ok = value != nullptr && settingsSaveBaseUrl(value);
    Serial.printf(ok ? "url=%s\r\n" : "rejected\r\n", settings.baseUrl);
    return;
  }

  if (strcmp(what, "key") == 0) {
    char *value = strtok(nullptr, " ");
    const bool ok = value != nullptr && settingsSaveApiKey(value);
    // Never echoed. It is committed in fleet.ini, which is an argument for
    // being careful with it rather than an argument for printing it.
    Serial.printf(ok ? "key saved\r\n" : "rejected\r\n");
    return;
  }

  if (strcmp(what, "baud") == 0) {
    char *value = strtok(nullptr, " ");
    const bool ok = value != nullptr && settingsSaveMavBaud((uint32_t)atol(value));
    Serial.printf(ok ? "baud=%lu, reboot to apply\r\n" : "rejected (9600..921600)\r\n",
                  (unsigned long)settings.mavBaud);
    return;
  }

  if (strcmp(what, "rate") == 0) {
    char *value = strtok(nullptr, " ");
    const bool ok = value != nullptr && settingsSaveLogRate((uint32_t)atol(value));
    Serial.printf(ok ? "rate=%lu Hz\r\n" : "rejected (1..10)\r\n",
                  (unsigned long)settings.logRateHz);
    return;
  }

  if (strcmp(what, "upload") == 0) {
    char *value = strtok(nullptr, " ");
    if (value == nullptr) {
      Serial.printf("usage: set upload on|off\r\n");
      return;
    }
    const bool enabled = strcmp(value, "on") == 0;
    settingsSaveUploadEnabled(enabled);
    Serial.printf("upload=%s\r\n", enabled ? "on" : "off");
    return;
  }

  Serial.printf("unknown setting\r\n");
}

static void handleLine(char *line) {
  char *command = strtok(line, " ");
  if (command == nullptr) return;

  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    Serial.print(Help);
    return;
  }
  if (strcmp(command, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(command, "flights") == 0) {
    printFlights();
    return;
  }
  if (strcmp(command, "head") == 0) {
    char *id = strtok(nullptr, " ");
    char *rows = strtok(nullptr, " ");
    if (id == nullptr) {
      Serial.printf("usage: head <id> [rows]\r\n");
      return;
    }
    printRows((uint16_t)atoi(id), 0, rows != nullptr ? (uint32_t)atol(rows) : 10);
    return;
  }
  if (strcmp(command, "tail") == 0) {
    char *id = strtok(nullptr, " ");
    char *rows = strtok(nullptr, " ");
    if (id == nullptr) {
      Serial.printf("usage: tail <id> [rows]\r\n");
      return;
    }
    handleTail((uint16_t)atoi(id), rows != nullptr ? (uint32_t)atol(rows) : 10);
    return;
  }
  if (strcmp(command, "rm") == 0) {
    char *id = strtok(nullptr, " ");
    if (id == nullptr) {
      Serial.printf("usage: rm <id>\r\n");
      return;
    }
    bool removed = false;
    if (storeLockLog(1000)) {
      removed = storeFlightLog().removeFlight((uint16_t)atoi(id));
      storeUnlockLog();
    }
    Serial.printf(removed ? "deleted\r\n" : "not deleted\r\n");
    return;
  }
  if (strcmp(command, "set") == 0) {
    handleSet(strtok(nullptr, ""));
    return;
  }
  if (strcmp(command, "reset") == 0) {
    Serial.printf(settingsReset() ? "settings reset\r\n" : "reset failed\r\n");
    return;
  }
  if (strcmp(command, "reboot") == 0) {
    Serial.printf("rebooting\r\n");
    delay(100);
    ESP.restart();
    return;
  }
  if (strcmp(command, "header") == 0) {
    Serial.printf("%s\r\n", CSV_HEADER);
    return;
  }

  Serial.printf("unknown command, try help\r\n");
}

static void shellTask(void *) {
  static char line[160];
  size_t length = 0;

  Serial.printf("\r\nflight telemetry logger. `help` for commands.\r\n");

  for (;;) {
    while (Serial.available() > 0) {
      const char character = (char)Serial.read();

      if (character == '\r' || character == '\n') {
        if (length > 0) {
          line[length] = 0;
          Serial.printf("\r\n");
          handleLine(line);
          length = 0;
        }
        continue;
      }

      if (character == 8 || character == 127) {
        if (length > 0) length--;
        continue;
      }

      if (length < sizeof(line) - 1) line[length++] = character;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void shellTaskStart() {
  xTaskCreatePinnedToCore(shellTask, "shell", 6144, nullptr, 1, nullptr, 0);
}
