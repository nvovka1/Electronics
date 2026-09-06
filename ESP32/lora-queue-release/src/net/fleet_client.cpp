#include "net/fleet_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app/safe_mode.h"
#include "core/calib.h"
#include "core/config.h"
#include "core/log.h"
#include "core/netcfg.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "net/net_task.h"
#include "net/root_ca.h"
#include "tasks/radio_task.h"

// Both clients are file-scope rather than stack locals. A WiFiClientSecure is
// a few hundred bytes of object but it anchors mbedtls contexts that must
// outlive the HTTPClient using them, and a task stack is not where either
// belongs. Only the network task ever calls into this file, so there is one
// user and no locking.
static WiFiClient s_plain;
static WiFiClientSecure s_tls;

// The free tier of the hosting service sleeps after about fifteen minutes
// without traffic and takes tens of seconds to wake. A node reporting every
// five minutes is very often the request that wakes it, so the timeout has to
// cover a cold start rather than a warm response.
static constexpr uint16_t CONNECT_TIMEOUT_MS = 12000;
static constexpr uint16_t RESPONSE_TIMEOUT_MS = 45000;

static void buildUrl(char *out, size_t max, const char *suffix) {
  snprintf(out, max, "%s/api/v1/devices/%s/%s", netcfgBaseUrl(), calibSerial(),
           suffix);
}

// Returns the client HTTPClient should use, or nullptr when this node is in no
// position to make a request at all.
static WiFiClient *prepareClient(const char *url) {
  if (WiFi.status() != WL_CONNECTED) return nullptr;

  if (strncmp(url, "https://", 8) != 0) return &s_plain;

  if (config().tls_verify) {
    // A certificate is a claim about a window of time. Without a clock the
    // node cannot judge notBefore/notAfter, and mbedtls fails the handshake
    // with an error that reads like a broken CA. Refusing here says the true
    // thing instead.
    if (!netTimeIsSynced()) return nullptr;
    s_tls.setCACert(FLEET_ROOT_CA_PEM);
  } else {
    // Deliberate, logged every single time. Without verification the API key
    // in the header is handed to whoever answers on that address.
    s_tls.setInsecure();
    LOG_W(TAG_NET, E_TLS_INSECURE, 0);
  }

  return &s_tls;
}

static bool beginRequest(HTTPClient &http, WiFiClient *client, const char *url) {
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);
  http.setReuse(false);
  // A redirect would carry the API key to whatever host the response names,
  // which is a credential leak one misconfigured proxy away.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!http.begin(*client, url)) return false;

  http.addHeader("X-Api-Key", netcfgApiKey());
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "lora-queue/" FW_SEMVER);
  return true;
}

// --- health report --------------------------------------------------------

int fleetPostHealth(fleet_checkin_t &out) {
  memset(&out, 0, sizeof(out));

  if (!netcfgIsProvisioned()) return FLEET_ERR_NOT_READY;

  char url[NET_URL_MAX + 64];
  buildUrl(url, sizeof(url), "health-reports");

  WiFiClient *client = prepareClient(url);
  if (!client) return FLEET_ERR_NOT_READY;

  JsonDocument doc;
  doc["nodeId"] = config().node_id;
  doc["hardwareId"] = FW_HW_ID;
  doc["firmwareVersion"] = FW_SEMVER;
  doc["firmwareHash"] = FW_GIT_HASH;
  doc["firmwareIsDirty"] = fwIsDirty();
  doc["buildType"] = FW_BUILD_TYPE;
  doc["protocolVersion"] = PROTO_VERSION;
  doc["configVersion"] = config().cfg_version;
  doc["uptimeSeconds"] = millis() / 1000u;
  doc["lastCrashCode"] = lastCrashCode();
  // Zero is not "flat", it is "the ADC self-test failed and this node does not
  // believe its own reading". The dashboard renders the two differently.
  doc["batteryDeciVolts"] = batteryTrusted() ? batteryDeciVolts() : 0;
  doc["postMask"] = postMask();

  // Both of these are clamped to the range the service validates against. The
  // service rejects the WHOLE report if one field is out of range, so a garbage
  // SPI read of the RSSI register or a corrupted boot counter would take a node
  // permanently off the air rather than costing it one bad number.
  doc["reboots"] = (uint32_t)min<uint32_t>(totalBootCount(), 2147483647u);
  doc["lastRssi"] = constrain(radioLastRssi(), -200, 50);

  char body[512];
  const size_t len = serializeJson(doc, body, sizeof(body));

  // serializeJson truncates rather than failing, and a truncated body is
  // invalid JSON that comes back as a 400 nobody can explain.
  if (len == 0 || len >= sizeof(body) - 1) return FLEET_ERR_BAD_RESPONSE;

  HTTPClient http;
  if (!beginRequest(http, client, url)) return FLEET_ERR_NOT_READY;

  const int status = http.POST((uint8_t *)body, len);

  if (status == HTTP_CODE_ACCEPTED || status == HTTP_CODE_OK) {
    JsonDocument response;
    if (deserializeJson(response, http.getStream()) == DeserializationError::Ok) {
      out.update_available = response["updateAvailable"] | false;
      const char *target = response["targetFirmwareVersion"] | "";
      strncpy(out.target_version, target, sizeof(out.target_version) - 1);
    }
    LOG_I(TAG_NET, E_REPORT_OK, status);
  } else {
    LOG_W(TAG_NET, E_REPORT_FAIL, status);
  }

  http.end();
  return status;
}

// --- log upload -----------------------------------------------------------

int fleetPostLogs(const log_rec_t *records, uint16_t count) {
  if (!records || count == 0) return HTTP_CODE_ACCEPTED;
  if (!netcfgIsProvisioned()) return FLEET_ERR_NOT_READY;

  char url[NET_URL_MAX + 64];
  buildUrl(url, sizeof(url), "log-records");

  WiFiClient *client = prepareClient(url);
  if (!client) return FLEET_ERR_NOT_READY;

  JsonDocument doc;
  JsonArray array = doc["records"].to<JsonArray>();
  for (uint16_t i = 0; i < count; i++) {
    JsonObject record = array.add<JsonObject>();
    // The node's own monotonic clock, in milliseconds since it booted. Never
    // wall-clock: these nodes have no RTC, and the absolute time is stamped on
    // receipt at the other end.
    record["timestampMs"] = records[i].ts_ms;
    record["level"] = records[i].lvl;
    record["tag"] = records[i].tag;
    record["code"] = records[i].code;
    record["arg"] = records[i].arg;
  }

  HTTPClient http;
  if (!beginRequest(http, client, url)) return FLEET_ERR_NOT_READY;

  String body;
  serializeJson(doc, body);
  const int status = http.POST(body);

  if (status == HTTP_CODE_ACCEPTED || status == HTTP_CODE_OK)
    LOG_I(TAG_NET, E_LOGS_SENT, count);
  else
    LOG_W(TAG_NET, E_LOGS_FAIL, status);

  http.end();
  return status;
}

// --- target firmware ------------------------------------------------------

int fleetGetTargetFirmware(ota_manifest_t &out) {
  memset(&out, 0, sizeof(out));

  if (!netcfgIsProvisioned()) return FLEET_ERR_NOT_READY;

  char url[NET_URL_MAX + 64];
  buildUrl(url, sizeof(url), "target-firmware");

  WiFiClient *client = prepareClient(url);
  if (!client) return FLEET_ERR_NOT_READY;

  HTTPClient http;
  if (!beginRequest(http, client, url)) return FLEET_ERR_NOT_READY;

  int status = http.GET();

  if (status == HTTP_CODE_OK) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) != DeserializationError::Ok) {
      status = FLEET_ERR_BAD_RESPONSE;
    } else {
      strncpy(out.version, doc["version"] | "", sizeof(out.version) - 1);
      strncpy(out.git_hash, doc["gitHash"] | "", sizeof(out.git_hash) - 1);
      strncpy(out.build_type, doc["buildType"] | "", sizeof(out.build_type) - 1);
      strncpy(out.hardware_id, doc["hardwareId"] | "", sizeof(out.hardware_id) - 1);
      strncpy(out.sha256, doc["sha256"] | "", sizeof(out.sha256) - 1);
      strncpy(out.download_url, doc["downloadUrl"] | "", sizeof(out.download_url) - 1);
      out.proto_version = doc["protocolVersion"] | 0u;
      out.size_bytes = doc["sizeBytes"] | 0u;

      // A manifest missing either of these describes an image that cannot be
      // verified, and an image that cannot be verified is never worth
      // downloading. Treated as a bad response, not as an offer.
      if (out.sha256[0] == '\0' || out.download_url[0] == '\0' || out.size_bytes == 0)
        status = FLEET_ERR_BAD_RESPONSE;
    }
  }

  LOG_I(TAG_OTA, E_OTA_CHECK, status);

  http.end();
  return status;
}

const char *fleetErrorText(int status) {
  switch (status) {
    case FLEET_ERR_NOT_READY:
      return "no link, no credentials, or no clock for TLS";
    case FLEET_ERR_BAD_RESPONSE:
      return "the service answered something this firmware cannot use";
    case HTTPC_ERROR_CONNECTION_REFUSED: return "connection refused";
    case HTTPC_ERROR_CONNECTION_LOST: return "connection lost";
    case HTTPC_ERROR_READ_TIMEOUT: return "read timeout";
    case HTTPC_ERROR_NO_STREAM: return "no stream";
    case 401: return "401 - the API key was rejected";
    case 404: return "404 - this serial is not enrolled";
    case 500: return "500 - the service failed";
    default: break;
  }
  return (status >= 200 && status < 300) ? "ok" : "see the status";
}
