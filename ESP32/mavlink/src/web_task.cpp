#include "web_task.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>

#include "config.h"
#include "csv_row.h"
#include "log.h"
#include "log_task.h"
#include "mav_task.h"
#include "net_task.h"
#include "settings.h"
#include "store.h"

static WebServer _server(80);

// One page, no external anything. A board serving its own page at a field has
// no route to a CDN, so a stylesheet or a charting library fetched from the
// internet would leave a blank screen exactly when the page is needed.
static const char PageHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flight telemetry</title>
<style>
:root{color-scheme:dark light}
body{font:14px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;margin:0;padding:16px;
background:#14161a;color:#e6e6e6}
h1{font-size:16px;margin:0 0 4px}
h2{font-size:14px;margin:24px 0 8px;color:#9aa4b2;font-weight:600}
.sub{color:#9aa4b2;margin-bottom:16px}
table{border-collapse:collapse;width:100%;max-width:900px}
td,th{text-align:left;padding:4px 10px 4px 0;border-bottom:1px solid #262a31;
white-space:nowrap}
th{color:#9aa4b2;font-weight:600}
a{color:#7fb2ff}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));
gap:8px;max-width:900px}
.cell{background:#1b1e24;border:1px solid #262a31;border-radius:6px;padding:8px 10px}
.cell .k{color:#9aa4b2;font-size:11px;text-transform:uppercase;letter-spacing:.04em}
.cell .v{font-size:16px}
.ok{color:#5fd38d}.bad{color:#ff7b72}.warn{color:#ffc857}
pre{background:#1b1e24;border:1px solid #262a31;border-radius:6px;padding:10px;
overflow:auto;max-width:900px;font-size:12px}
button{font:inherit;background:#262a31;color:#e6e6e6;border:1px solid #3a4049;
border-radius:4px;padding:2px 8px;cursor:pointer}
</style></head><body>
<h1>Flight telemetry</h1>
<div class="sub" id="who">&nbsp;</div>
<div class="grid" id="live"></div>
<h2>Flights</h2>
<table id="flights"><thead><tr><th>flight<th>rows<th>size<th>uploaded<th>
</tr></thead><tbody></tbody></table>
<h2>Log</h2>
<pre id="log">&nbsp;</pre>
<script>
const bytes = n => n < 1024 ? n + " B"
  : n < 1048576 ? (n/1024).toFixed(1) + " kB" : (n/1048576).toFixed(2) + " MB";

const cell = (k, v, cls) =>
  `<div class="cell"><div class="k">${k}</div><div class="v ${cls||''}">${v}</div></div>`;

const show = v => (v === null || v === undefined || v === "") ? "&mdash;" : v;

async function tick() {
  let s;
  try { s = await (await fetch("/api/status")).json(); } catch (e) { return; }

  const linkClass = s.linkAgeMs === null ? "bad" : s.linkAgeMs < 3000 ? "ok" : "warn";
  const link = s.linkAgeMs === null ? "no link" : (s.linkAgeMs/1000).toFixed(1) + " s ago";

  document.getElementById("who").innerHTML =
    `${s.serial} &middot; flight ${s.flightId} &middot; ${s.address} &middot; ` +
    `${bytes(s.fsUsed)} of ${bytes(s.fsTotal)} used`;

  const t = s.telemetry;
  document.getElementById("live").innerHTML = [
    cell("heartbeat", link, linkClass),
    cell("armed", t.armed === null ? "&mdash;" : (t.armed ? "ARMED" : "disarmed"),
         t.armed ? "bad" : "ok"),
    cell("mode", show(t.mode)),
    cell("battery", t.batteryVoltage === null ? "&mdash;" :
         t.batteryVoltage.toFixed(2) + " V"),
    cell("current", t.batteryCurrent === null ? "&mdash;" :
         t.batteryCurrent.toFixed(1) + " A"),
    cell("used", t.consumedMah === null ? "&mdash;" : t.consumedMah + " mAh"),
    cell("gps fix", show(t.gpsFix)),
    cell("sats", show(t.sats)),
    cell("hdop", t.hdop === null ? "&mdash;" : t.hdop.toFixed(2)),
    cell("lat", t.lat === null ? "&mdash;" : t.lat.toFixed(6)),
    cell("lon", t.lon === null ? "&mdash;" : t.lon.toFixed(6)),
    cell("alt rel", t.altRel === null ? "&mdash;" : t.altRel.toFixed(1) + " m"),
    cell("ground spd", t.groundSpeed === null ? "&mdash;" :
         t.groundSpeed.toFixed(1) + " m/s"),
    cell("climb", t.climb === null ? "&mdash;" : t.climb.toFixed(1) + " m/s"),
    cell("heading", t.heading === null ? "&mdash;" : Math.round(t.heading) + "&deg;"),
    cell("rc rssi", show(t.rcRssi)),
    cell("wifi rssi", t.wifiRssi === null ? "&mdash;" : t.wifiRssi + " dBm"),
    cell("rows", s.rowsWritten),
    cell("uploaded", s.rowsUploaded),
    cell("http", s.lastHttpStatus === 0 ? "&mdash;" : s.lastHttpStatus,
         s.lastHttpStatus >= 200 && s.lastHttpStatus < 300 ? "ok" : "warn"),
  ].join("");

  const rows = s.flights.map(f => {
    const done = f.uploaded >= f.bytes;
    return `<tr><td>${f.id}<td>${f.rows}<td>${bytes(f.bytes)}` +
      `<td class="${done ? 'ok' : 'warn'}">${done ? 'yes' : Math.floor(100*f.uploaded/Math.max(1,f.bytes)) + '%'}` +
      `<td><a href="/flights/${f.id}.csv" download>csv</a> ` +
      `<button onclick="del(${f.id})">delete</button>`;
  }).join("");
  document.querySelector("#flights tbody").innerHTML = rows || "<tr><td>none";

  const log = await (await fetch("/api/log")).text();
  document.getElementById("log").textContent = log;
}

async function del(id) {
  if (!confirm("Delete flight " + id + "?")) return;
  await fetch("/flights/" + id, {method: "DELETE"});
  tick();
}

tick();
setInterval(tick, 1000);
</script></body></html>)HTML";

// A number, or JSON null. Every unknown reaches the page as null rather than
// zero, exactly as it reaches the CSV as an empty cell - so the page can say
// "nobody has reported this" instead of showing a plausible wrong number.
static void appendReal(String &json, const char *name, double value, int decimals,
                       bool known) {
  json += "\"";
  json += name;
  json += "\":";
  if (!known) {
    json += "null";
    return;
  }
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  json += buffer;
}

static void appendInteger(String &json, const char *name, long long value, bool known) {
  json += "\"";
  json += name;
  json += "\":";
  json += known ? String((long)value) : String("null");
}

static void handleStatus() {
  TelemetrySnapshot snapshot;
  storeSnapshot(snapshot);

  const uint32_t linkAge = mavLinkAgeMs();

  String json;
  json.reserve(1600);

  json += "{\"serial\":\"";
  json += settings.serial;
  json += "\",\"address\":\"";
  json += netAddress();
  json += "\",\"flightId\":";
  json += logFlightId();
  json += ",\"rowsWritten\":";
  json += logRowsWritten();
  json += ",\"rowsUploaded\":";
  json += netRowsUploaded();
  json += ",\"lastHttpStatus\":";
  json += netLastHttpStatus();
  json += ",\"bytesSeen\":";
  json += mavBytesSeen();
  json += ",\"messagesSeen\":";
  json += mavMessagesSeen();
  json += ",\"parseErrors\":";
  json += mavParseErrors();
  json += ",\"fsUsed\":";
  json += storeFsUsedBytes();
  json += ",\"fsTotal\":";
  json += storeFsTotalBytes();
  json += ",\"linkAgeMs\":";
  json += linkAge == UINT32_MAX ? String("null") : String((unsigned long)linkAge);

  json += ",\"telemetry\":{";
  appendInteger(json, "armed", snapshot.armed ? 1 : 0, snapshot.haveHeartbeat);
  json += ",";
  appendInteger(json, "mode", snapshot.mode, snapshot.haveHeartbeat);
  json += ",";
  appendInteger(json, "gpsFix", snapshot.gpsFix, snapshot.gpsFix != UNKNOWN_I8);
  json += ",";
  appendInteger(json, "sats", snapshot.sats, snapshot.sats != UNKNOWN_I8);
  json += ",";
  appendReal(json, "hdop", snapshot.hdop, 2, !isnan(snapshot.hdop));
  json += ",";
  appendReal(json, "lat", snapshot.lat, 7, !isnan(snapshot.lat));
  json += ",";
  appendReal(json, "lon", snapshot.lon, 7, !isnan(snapshot.lon));
  json += ",";
  appendReal(json, "altRel", snapshot.altRel, 2, !isnan(snapshot.altRel));
  json += ",";
  appendReal(json, "groundSpeed", snapshot.groundSpeed, 2, !isnan(snapshot.groundSpeed));
  json += ",";
  appendReal(json, "climb", snapshot.climb, 2, !isnan(snapshot.climb));
  json += ",";
  appendReal(json, "heading", snapshot.heading, 1, !isnan(snapshot.heading));
  json += ",";
  appendReal(json, "batteryVoltage", snapshot.batteryVoltage, 3,
             !isnan(snapshot.batteryVoltage));
  json += ",";
  appendReal(json, "batteryCurrent", snapshot.batteryCurrent, 2,
             !isnan(snapshot.batteryCurrent));
  json += ",";
  appendInteger(json, "consumedMah", snapshot.consumedMah,
                snapshot.consumedMah != UNKNOWN_I32);
  json += ",";
  appendInteger(json, "rcRssi", snapshot.rcRssi, snapshot.rcRssi != UNKNOWN_I16);
  json += ",";
  appendInteger(json, "wifiRssi", snapshot.wifiRssi, snapshot.wifiRssi != UNKNOWN_I16);
  json += "}";

  json += ",\"flights\":[";

  if (storeLockLog(500)) {
    FlightInfo flights[FlightsMax];
    const uint32_t count = storeFlightLog().listFlights(flights, FlightsMax);

    // Newest first here, the opposite of the upload order. The page is read by
    // a person who has just landed and wants the flight they just flew.
    for (uint32_t i = count; i > 0; i--) {
      const FlightInfo &flight = flights[i - 1];
      if (i != count) json += ",";
      json += "{\"id\":";
      json += flight.id;
      json += ",\"bytes\":";
      json += flight.bytes;
      json += ",\"uploaded\":";
      json += flight.uploaded;
      json += ",\"rows\":";
      // Estimated from the size rather than counted: counting means reading the
      // whole file, and this page refreshes every second.
      json += flight.bytes > 0 ? (flight.bytes / 120) : 0;
      json += "}";
    }
    storeUnlockLog();
  }

  json += "]}";

  _server.send(200, "application/json", json);
}

static uint16_t requestedFlightId(const String &uri) {
  const int slash = uri.lastIndexOf('/');
  if (slash < 0) return 0;
  return (uint16_t)uri.substring(slash + 1).toInt();
}

static void handleCsv() {
  const String uri = _server.uri();
  const uint16_t id = requestedFlightId(uri);
  if (id == 0) {
    _server.send(404, "text/plain", "no such flight\n");
    return;
  }

  char path[32];
  flightLogCsvPath(id, path, sizeof(path));

  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    _server.send(404, "text/plain", "no such flight\n");
    return;
  }

  char attachment[64];
  snprintf(attachment, sizeof(attachment), "attachment; filename=\"flight-%u.csv\"",
           (unsigned)id);
  _server.sendHeader("Content-Disposition", attachment);

  // streamFile rather than a read into a String: a flight is up to two
  // megabytes and the board has a few hundred kilobytes of heap.
  _server.streamFile(file, "text/csv");
  file.close();
}

static void handleDelete() {
  const uint16_t id = requestedFlightId(_server.uri());
  if (id == 0) {
    _server.send(404, "text/plain", "no such flight\n");
    return;
  }

  bool removed = false;
  if (storeLockLog(1000)) {
    removed = storeFlightLog().removeFlight(id);
    storeUnlockLog();
  }

  LOG_INFO("web", "delete flight %u: %s", (unsigned)id, removed ? "ok" : "failed");
  _server.send(removed ? 200 : 409, "text/plain", removed ? "deleted\n" : "busy\n");
}

static void handleLog() {
  char buffer[2600];
  logRecent(buffer, sizeof(buffer));
  _server.send(200, "text/plain", buffer);
}

static void webTask(void *) {
  _server.on("/", HTTP_GET, []() {
    _server.send_P(200, "text/html", PageHtml);
  });
  _server.on("/api/status", HTTP_GET, handleStatus);
  _server.on("/api/log", HTTP_GET, handleLog);

  // Both shapes are routed by hand because the flight id is in the path and
  // WebServer has no path parameters.
  _server.onNotFound([]() {
    const String uri = _server.uri();

    if (_server.method() == HTTP_GET && uri.startsWith("/flights/") && uri.endsWith(".csv")) {
      handleCsv();
      return;
    }
    if (_server.method() == HTTP_DELETE && uri.startsWith("/flights/")) {
      handleDelete();
      return;
    }

    _server.send(404, "text/plain", "not found\n");
  });

  // WiFiServer::begin() opens an lwIP socket, and lwIP's mailbox does not exist
  // until an interface is up. Calling it before then asserts inside the TCP/IP
  // thread and takes the whole board down.
  //
  // This was always a race with the network task, and one this task happened to
  // win on most boots. It loses every time once WiFi is held off after a
  // brownout, because then there is no interface coming at all - so the wait is
  // unbounded on purpose. A board with no network has nothing to serve, and the
  // recording does not care either way.
  while (!netIsStationConnected() && !netIsAccessPoint()) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  _server.begin();
  LOG_INFO("web", "serving on %s port 80", netAddress().c_str());

  for (;;) {
    _server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void webTaskStart() { xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0); }
