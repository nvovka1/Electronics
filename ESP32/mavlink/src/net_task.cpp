#include "net_task.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "csv_row.h"
#include "log.h"
#include "settings.h"
#include "store.h"

// FILE SCOPE, NOT A LOCAL. A WiFiClientSecure built inside the request function
// takes its TLS buffers from the stack of whichever task is running and the
// handshake then hangs forever rather than failing - a symptom that looks like
// the server being unreachable and wastes an afternoon every time.
static WiFiClientSecure _secureClient;
static WiFiClient _plainClient;

// Allocated once. A 16 kB body built and freed on every batch fragments the
// heap that the TLS handshake also needs, and the handshake is the allocation
// that fails first.
static char *_chunk = nullptr;
static char *_body = nullptr;
static const uint32_t BodyBytes = 16384;

static bool _stationConnected = false;
static bool _accessPointActive = false;
static int _lastStatus = 0;
static uint32_t _rowsUploaded = 0;
static uint32_t _backoffMs = 0;

bool netIsStationConnected() { return _stationConnected; }
bool netIsAccessPoint() { return _accessPointActive; }
int netLastHttpStatus() { return _lastStatus; }
uint32_t netRowsUploaded() { return _rowsUploaded; }

String netAddress() {
  if (_stationConnected) return WiFi.localIP().toString();
  if (_accessPointActive) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

// ------------------------------------------------------------------- joining

static bool joinStation() {
  if (strlen(settings.ssid) == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(settings.ssid, settings.password);

  LOG_INFO("net", "joining %s", settings.ssid);

  const uint32_t started = millis();
  while (millis() - started < WifiJoinTimeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      LOG_INFO("net", "joined, ip=%s rssi=%d", WiFi.localIP().toString().c_str(),
               (int)WiFi.RSSI());
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  LOG_WARN("net", "could not join %s", settings.ssid);
  return false;
}

static void raiseAccessPoint() {
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(ApSsid, ApPassword)) {
    _accessPointActive = true;
    LOG_INFO("net", "ap %s up at %s", ApSsid, WiFi.softAPIP().toString().c_str());
  } else {
    LOG_ERROR("net", "ap failed");
  }
}

// ---------------------------------------------------------------- the upload

// Wraps one chunk of CSV rows as the request body. Returns the number of rows
// it managed to include, and fills `firstIndex` from the first of them.
static uint32_t buildBody(uint16_t flightId, const char *chunk, uint32_t chunkLength,
                          uint32_t *firstIndex) {
  uint32_t length = 0;
  uint32_t rows = 0;
  *firstIndex = UINT32_MAX;

  length += snprintf(_body + length, BodyBytes - length, "{\"flightId\":%u,\"firstIndex\":",
                     (unsigned)flightId);

  // Placeholder: the first index is not known until the first row has parsed,
  // and the alternative is walking the chunk twice.
  const uint32_t firstIndexAt = length;
  length += snprintf(_body + length, BodyBytes - length, "%10s", "0");
  length += snprintf(_body + length, BodyBytes - length, ",\"samples\":[");

  uint32_t cursor = 0;
  while (cursor < chunkLength) {
    uint32_t end = cursor;
    while (end < chunkLength && chunk[end] != '\n') end++;

    const uint32_t lineLength = end - cursor;
    const char *line = chunk + cursor;
    cursor = end + 1;

    if (lineLength == 0) continue;

    const uint32_t index = csvRowIndex(line, lineLength);
    // The header, or anything else that is not a row. Skipped rather than
    // refused: the header is legitimately in the file.
    if (index == UINT32_MAX) continue;

    char json[512];
    const size_t jsonLength = csvRowToJson(line, lineLength, json, sizeof(json));
    if (jsonLength == 0) {
      LOG_WARN("net", "flight %u row %lu unconvertible", (unsigned)flightId,
               (unsigned long)index);
      continue;
    }

    // Room for this row, the comma and the closing "]}".
    if (length + jsonLength + 8 > BodyBytes) break;

    if (rows > 0) _body[length++] = ',';
    memcpy(_body + length, json, jsonLength);
    length += jsonLength;
    _body[length] = 0;

    if (*firstIndex == UINT32_MAX) *firstIndex = index;
    rows++;
  }

  length += snprintf(_body + length, BodyBytes - length, "]}");

  if (rows == 0) return 0;

  // Back-fill the placeholder. Ten characters is wider than any uint32 needs,
  // so the number is written left-aligned into the same space and the JSON
  // stays valid - whitespace between a colon and a value is legal.
  char number[12];
  snprintf(number, sizeof(number), "%-10lu", (unsigned long)*firstIndex);
  memcpy(_body + firstIndexAt, number, 10);

  return rows;
}

static int postBody(uint32_t *nextIndex) {
  char url[256];
  snprintf(url, sizeof(url), "%s/api/v1/aircraft/%s/telemetry-samples", settings.baseUrl,
           settings.serial);

  const bool secure = strncmp(settings.baseUrl, "https", 5) == 0;

  HTTPClient http;
  http.setTimeout(HttpTimeoutMs);
  http.setConnectTimeout(HttpTimeoutMs);
  http.setReuse(false);

  const bool opened =
      secure ? http.begin(_secureClient, url) : http.begin(_plainClient, url);
  if (!opened) return -1000;

  http.addHeader("Content-Type", "application/json");
  http.addHeader(F("X-Api-Key"), settings.apiKey);

  const int status = http.POST((uint8_t *)_body, strlen(_body));

  if (status >= 200 && status < 300) {
    const String payload = http.getString();
    JsonDocument document;
    if (deserializeJson(document, payload) == DeserializationError::Ok) {
      if (!document["nextIndex"].isNull()) {
        *nextIndex = document["nextIndex"].as<uint32_t>();
      }
    }
  }

  http.end();
  return status;
}

// How many bytes of the chunk the service has actually taken, given the index
// it says it wants next. Walking the chunk we still hold is exact and costs
// nothing; the alternative is a byte offset per index kept on flash.
static uint32_t acceptedBytes(const char *chunk, uint32_t chunkLength, uint32_t nextIndex) {
  uint32_t cursor = 0;
  uint32_t accepted = 0;

  while (cursor < chunkLength) {
    uint32_t end = cursor;
    while (end < chunkLength && chunk[end] != '\n') end++;
    const uint32_t lineLength = end - cursor;
    const uint32_t consumed = (end < chunkLength) ? (lineLength + 1) : lineLength;

    const uint32_t index = csvRowIndex(chunk + cursor, lineLength);
    // The header counts as accepted: the service never wanted it and leaving it
    // unaccounted for would stall the cursor at byte zero forever.
    if (index != UINT32_MAX && index >= nextIndex) break;

    accepted += consumed;
    cursor = end + 1;
  }

  return accepted;
}

enum UploadOutcome { UploadNothingToDo, UploadProgressed, UploadFailed };

static UploadOutcome uploadOnce() {
  if (!storeLockLog(500)) return UploadNothingToDo;

  FlightLog &flightLog = storeFlightLog();

  FlightInfo flights[FlightsMax];
  const uint32_t count = flightLog.listFlights(flights, FlightsMax);

  uint16_t flightId = 0;
  uint32_t offset = 0;
  uint32_t chunkLength = 0;

  for (uint32_t i = 0; i < count; i++) {
    if (flights[i].uploaded >= flights[i].bytes) continue;
    flightId = flights[i].id;
    offset = flights[i].uploaded;
    chunkLength = flightLog.readChunk(flightId, offset, _chunk, UploadChunkBytes);
    break;
  }

  storeUnlockLog();

  if (flightId == 0) return UploadNothingToDo;

  // No complete row past the cursor. Happens between the moment the logger
  // starts a row and the moment it finishes one; waiting is the whole fix.
  if (chunkLength == 0) return UploadNothingToDo;

  uint32_t firstIndex = 0;
  const uint32_t rows = buildBody(flightId, _chunk, chunkLength, &firstIndex);

  if (rows == 0) {
    // Nothing convertible in the chunk at all. Advancing past it stops the
    // cursor jamming on rows that will never be accepted; they are still in the
    // file on the board.
    if (storeLockLog(500)) {
      storeFlightLog().setUploaded(flightId, offset + chunkLength);
      storeUnlockLog();
    }
    LOG_WARN("net", "flight %u chunk skipped at %lu", (unsigned)flightId,
             (unsigned long)offset);
    return UploadProgressed;
  }

  uint32_t nextIndex = firstIndex + rows;
  const int status = postBody(&nextIndex);
  _lastStatus = status;

  if (status < 200 || status >= 300) {
    LOG_WARN("net", "flight %u upload failed, status=%d", (unsigned)flightId, status);
    return UploadFailed;
  }

  // The service is behind us: it is missing rows we thought it had. Rewind to
  // the start of the rows and send the flight again. Duplicates cost nothing -
  // the unique index on the far side discards them - and this is the only way
  // a lost acknowledgement repairs itself.
  if (nextIndex < firstIndex) {
    const uint32_t headerEnd = strlen(CSV_HEADER) + 1;
    if (storeLockLog(500)) {
      storeFlightLog().setUploaded(flightId, headerEnd);
      storeUnlockLog();
    }
    LOG_WARN("net", "flight %u rewound: service wants %lu, we sent from %lu",
             (unsigned)flightId, (unsigned long)nextIndex, (unsigned long)firstIndex);
    return UploadProgressed;
  }

  const uint32_t accepted = acceptedBytes(_chunk, chunkLength, nextIndex);

  if (accepted == 0) {
    LOG_WARN("net", "flight %u no progress at %lu", (unsigned)flightId, (unsigned long)offset);
    return UploadFailed;
  }

  if (storeLockLog(500)) {
    storeFlightLog().setUploaded(flightId, offset + accepted);
    storeUnlockLog();
  }

  _rowsUploaded += rows;
  LOG_DEBUG("net", "flight %u +%lu rows, cursor %lu", (unsigned)flightId, (unsigned long)rows,
            (unsigned long)(offset + accepted));

  return UploadProgressed;
}

// ------------------------------------------------------------------ the task

static void netTask(void *) {
  _chunk = (char *)malloc(UploadChunkBytes + 1);
  _body = (char *)malloc(BodyBytes);

  if (_chunk == nullptr || _body == nullptr) {
    LOG_ERROR("net", "no heap for upload buffers, uploading disabled");
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
  }

  _stationConnected = joinStation();
  if (!_stationConnected) raiseAccessPoint();

  uint32_t lastRssiMs = 0;

  for (;;) {
    const uint32_t now = millis();

    if (_stationConnected && WiFi.status() != WL_CONNECTED) {
      LOG_WARN("net", "station dropped");
      _stationConnected = false;
    }

    if (!_stationConnected && !_accessPointActive) {
      _stationConnected = joinStation();
      if (!_stationConnected) raiseAccessPoint();
    }

    if (now - lastRssiMs >= 2000) {
      lastRssiMs = now;
      // Into the snapshot, so it lands in the rows. "The upload stopped" and
      // "the aircraft flew out of range" are one event seen from two sides, and
      // the row should say which.
      storeSetWifiRssi(_stationConnected ? (int16_t)WiFi.RSSI() : UNKNOWN_I16);
    }

    if (!_stationConnected || !settings.uploadEnabled) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (_backoffMs > 0) {
      vTaskDelay(pdMS_TO_TICKS(_backoffMs));
      _backoffMs = 0;
    }

    switch (uploadOnce()) {
      case UploadProgressed:
        // Straight back round. A flight recorded while the board was offline is
        // megabytes behind and there is no reason to trickle it.
        vTaskDelay(pdMS_TO_TICKS(50));
        break;

      case UploadFailed:
        _backoffMs = _backoffMs == 0 ? 2000 : _backoffMs * 2;
        if (_backoffMs > UploadBackoffMsMax) _backoffMs = UploadBackoffMsMax;
        break;

      case UploadNothingToDo:
      default:
        vTaskDelay(pdMS_TO_TICKS(UploadIdleDelayMs));
        break;
    }
  }
}

void netTaskStart() {
  // Certificate pinning would be better and is not free: Render rotates its
  // chain, and a board in a field with an expired pin is a board that has
  // silently stopped reporting. The key in the header is what authenticates
  // this, and TLS is here for confidentiality on the way.
  _secureClient.setInsecure();

  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
