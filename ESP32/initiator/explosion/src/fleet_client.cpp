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
int request(const char *method, const String &path, const String &body, String *response) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  // The certificate is not checked. Worth being plain about: this protects the
  // API key and the traffic from passive capture, not from someone who can
  // stand in the middle. Pinning the root would need the CA bundle kept in step
  // with whatever the host rotates to, and a node that silently stops reporting
  // the day a certificate changes is its own failure. The key is the weak part
  // of this system either way, which is why it guards nothing destructive.
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HttpTimeoutMs / 1000);

  HTTPClient http;
  http.setTimeout(HttpTimeoutMs);
  http.setConnectTimeout(HttpTimeoutMs);

  const String url = String(settings.baseUrl) + path;
  if (!http.begin(client, url)) return -1;

  http.addHeader("X-Api-Key", settings.apiKey);
  http.addHeader("Content-Type", "application/json");

  const int status = (strcmp(method, "GET") == 0)
                         ? http.GET()
                         : http.sendRequest(method, (uint8_t *)body.c_str(), body.length());

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
