#include "fleet_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "board_pins.h"
#include "config.h"
#include "radio_task.h"
#include "settings.h"
#include "state_task.h"

namespace {

int lastHttpStatus = 0;

// FILE SCOPE, NOT A STACK LOCAL, and this is the whole reason the node could
// not reach the service.
//
// A WiFiClientSecure is a small object that anchors mbedTLS contexts several
// kilobytes in size, and those contexts have to outlive the HTTPClient using
// them. Built on the task stack it blocks inside the handshake and never comes
// back: the request goes out, no timeout fires, and the task simply stops - the
// heartbeat beside it stops in the same millisecond. Stack headroom looks fine
// right up to the call, because the damage is done inside it.
//
// Only the net task ever calls into this file, so one client is enough and
// there is nothing to lock.
WiFiClientSecure tls;

// The battery sits on a 100k/100k divider from VBAT on ADC1. Zero is not a flat
// battery: it is this node saying it could not take a reading it trusts, and
// the backend renders it as "untrusted" rather than as empty.
int batteryDeciVolts() {
  const uint32_t millivolts = analogReadMilliVolts(BatteryPin);
  if (millivolts == 0) return 0;

  const uint32_t actual = (millivolts * BatteryDividerQ10) / 1024U;
  return (int)(actual / 100U); // millivolts -> tenths of a volt
}

// One place that knows how to talk to the service. Returns the HTTP status, or
// a negative HTTPClient error.
// Distinct negative codes for the two ways this can fail before a request is
// ever sent. They existed as a bare "return -1" that never reached
// lastHttpStatus, so the screen and the log both showed "nothing attempted yet"
// for a request that had already failed - which is the worst possible way to
// report a failure, because it looks like patience is the answer.
constexpr int StatusNoWifi = -101;
constexpr int StatusBadUrl = -102;

int request(const char *method, const String &path, const String &body, String *response) {
  if (WiFi.status() != WL_CONNECTED) {
    lastHttpStatus = StatusNoWifi;
    return StatusNoWifi;
  }

  // The certificate is not checked. Worth being plain about: this protects the
  // API key and the traffic from passive capture, not from someone who can
  // stand in the middle. The key is the weak part of this system either way,
  // which is why it guards nothing destructive.
  tls.setInsecure();
  tls.setTimeout(HttpTimeoutMs / 1000);

  // Separate from setTimeout above. Its default is two minutes, during which a
  // stalled handshake is indistinguishable from a dead task.
  tls.setHandshakeTimeout(TlsHandshakeTimeoutSeconds);

  HTTPClient http;
  http.setTimeout(HttpTimeoutMs);
  http.setConnectTimeout(HttpTimeoutMs);

  // No connection reuse, and no redirect following. A redirect would carry the
  // API key to whatever host the response names, which is a credential leak one
  // misconfigured proxy away.
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  const String url = String(settings.baseUrl) + path;

  if (!http.begin(tls, url)) {
    // Almost always the base URL: empty, missing the scheme, or with something
    // stored in NVS that nobody remembers putting there.
    lastHttpStatus = StatusBadUrl;
    LOG_ERROR(TagNet, CodeReportFail, StatusBadUrl);
    return StatusBadUrl;
  }

  http.addHeader("X-Api-Key", settings.apiKey);
  http.addHeader("Content-Type", "application/json");

  // Logged BEFORE the call, and in request() rather than in each caller, so
  // every path is covered. A request that hangs or takes the task down with it
  // writes nothing on the way out - and the absence of a line then reads as
  // "it never tried", which is a completely different fault from "it tried and
  // nothing came back". Three minutes of silence after wifi_up looked like the
  // first and was the second.
  LOG_INFO(TagNet, CodeReportTry, (int32_t)body.length());

  // TLS wants tens of kilobytes of heap for its buffers and certificates. When
  // there is not enough it does not say so politely, so the number is recorded
  // here where it can be compared across attempts.
  LOG_INFO(TagNet, CodeHeapFree, (int32_t)ESP.getFreeHeap());

  const int status = (strcmp(method, "GET") == 0)
                         ? http.GET()
                         : http.sendRequest(method, (uint8_t *)body.c_str(), body.length());

  lastHttpStatus = status;

  if (response != nullptr && status > 0) *response = http.getString();

  http.end();
  return status;
}

bool isSuccess(int status) { return status >= 200 && status < 300; }

String devicePath(const char *suffix) {
  return String("/api/v1/devices/") + settings.serial + suffix;
}

} // namespace

bool fleetPostHealth(uint8_t state, uint8_t *serviceState) {
  JsonDocument doc;

  doc["nodeId"] = settings.nodeId;
  doc["hardwareId"] = "ttgo-lora32-v21";
  doc["firmwareVersion"] = "0.1.0";
  doc["buildType"] = FW_BUILD_TYPE;
  doc["protocolVersion"] = PROTO_VERSION;
  doc["uptimeSeconds"] = millis() / 1000UL;
  doc["reboots"] = settings.bootCount;
  doc["lastCrashCode"] = (int)esp_reset_reason();
  doc["batteryDeciVolts"] = batteryDeciVolts();
  doc["lastRssi"] = radioLastRssi();
  doc["postMask"] = 0;
  doc["loraLinkUp"] = radioIsReady();
  doc["state"] = state;
  doc["stateTimestampMs"] = millis();
  doc["autoArmTimeoutSeconds"] = settings.autoArmSeconds;

  String body;
  serializeJson(doc, body);

  String response;
  const int status = request("POST", devicePath("/health-reports"), body, &response);

  if (!isSuccess(status)) {
    LOG_WARN(TagNet, CodeReportFail, status);
    return false;
  }

  // The service hands its own view back, so a node whose state event went
  // missing finds out on the next report rather than waiting for its next
  // transition.
  if (serviceState != nullptr) {
    JsonDocument parsed;
    if (deserializeJson(parsed, response) == DeserializationError::Ok) {
      *serviceState = parsed["state"] | state;
    }
  }

  LOG_DEBUG(TagNet, CodeReportOk, status);
  return true;
}

bool fleetPostStateEvents(const StateEventRecord *events, uint8_t count, uint8_t *stored) {
  if (events == nullptr || count == 0) return true;

  JsonDocument doc;
  JsonArray array = doc["events"].to<JsonArray>();

  for (uint8_t i = 0; i < count; i++) {
    JsonObject item = array.add<JsonObject>();
    item["timestampMs"] = events[i].timestampMs;
    item["bootCount"] = events[i].bootCount;
    item["fromState"] = events[i].fromState;
    item["toState"] = events[i].toState;

    // Null, not zero. The auto-arm has no command behind it, and a zero here
    // would decode as a command code the backend does not know and get the
    // whole event discarded.
    if (events[i].hasCommand) {
      item["command"] = events[i].command;
    } else {
      item["command"] = nullptr;
    }

    item["source"] = events[i].source;
    item["accepted"] = events[i].accepted;
    item["reason"] = events[i].reason;

    // Set only when this transition came from a command the node collected
    // from the queue. The backend closes that queue entry off the same post,
    // so there is no second request to report the result.
    if (events[i].commandId[0] != '\0') item["commandId"] = events[i].commandId;
  }

  String body;
  serializeJson(doc, body);

  String response;
  const int status = request("POST", devicePath("/state-events"), body, &response);

  if (!isSuccess(status)) {
    LOG_WARN(TagNet, CodeReportFail, status);
    return false;
  }

  // Only what the backend says it kept may be dropped from the buffer.
  // Assuming all of them would silently lose any the service could not
  // understand.
  if (stored != nullptr) {
    JsonDocument parsed;
    *stored = (deserializeJson(parsed, response) == DeserializationError::Ok)
                  ? (uint8_t)(parsed["stored"] | count)
                  : count;
  }

  LOG_INFO(TagNet, CodeEventPosted, count);
  return true;
}

bool fleetPostLogs(const LogRecord *records, uint8_t count) {
  if (records == nullptr || count == 0) return true;

  JsonDocument doc;
  JsonArray array = doc["records"].to<JsonArray>();

  for (uint8_t i = 0; i < count; i++) {
    JsonObject item = array.add<JsonObject>();
    item["timestampMs"] = records[i].timestampMs;
    item["level"] = records[i].level;
    item["tag"] = records[i].tag;
    item["code"] = records[i].code;
    item["arg"] = records[i].arg;
  }

  String body;
  serializeJson(doc, body);

  const int status = request("POST", devicePath("/log-records"), body, nullptr);

  if (!isSuccess(status)) {
    LOG_WARN(TagNet, CodeReportFail, status);
    return false;
  }

  LOG_DEBUG(TagNet, CodeLogSent, count);
  return true;
}

bool fleetPollCommand(PolledCommand *out) {
  if (out == nullptr) return false;

  String response;
  const int status = request("GET", devicePath("/commands/next"), "", &response);

  // 204 is the common answer and the cheapest one to handle: nothing queued.
  if (status == 204) return false;

  if (!isSuccess(status)) {
    LOG_WARN(TagNet, CodeReportFail, status);
    return false;
  }

  JsonDocument parsed;
  if (deserializeJson(parsed, response) != DeserializationError::Ok) {
    LOG_WARN(TagNet, CodeReportFail, 0);
    return false;
  }

  const char *id = parsed["commandId"] | "";
  strncpy(out->commandId, id, COMMAND_ID_MAX - 1);
  out->commandId[COMMAND_ID_MAX - 1] = '\0';
  out->command = parsed["command"] | 0;

  if (out->commandId[0] == '\0' || !command_is_valid(out->command)) {
    // A command this build does not understand. Refusing it here is the only
    // safe answer: the alternative is handing the state machine a byte it will
    // reject anyway, from a source that cannot be told what happened.
    LOG_WARN(TagNet, CodeReportFail, out->command);
    return false;
  }

  LOG_INFO(TagNet, CodeCmdPolled, out->command);
  return true;
}

int fleetLastHttpStatus() { return lastHttpStatus; }
