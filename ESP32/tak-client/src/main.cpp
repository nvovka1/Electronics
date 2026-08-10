/*
 * ESP32 TAK client
 * ----------------
 * Reports position to a TAK Server so the board shows up as a live contact on
 * every connected ATAK screen.
 *
 *   pio run -e takclient -t upload
 *   pio run -e takclient -t monitor
 *
 * There is no registration handshake in CoT. The server creates the contact
 * from the uid and callsign inside the first position report, and keeps it
 * alive only while fresh reports keep arriving before each message's stale
 * time. The send-on-a-timer loop below IS the registration.
 *
 * Same LILYGO/TTGO LoRa32 board as the lora-morse project; LoRa is unused here.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "CotEvent.h"
#include "config.h"

#ifdef TAK_USE_TLS
  #include "TlsTransport.h"
  static TlsTransport transport;
#else
  #include "StcpTransport.h"
  static StcpTransport transport;
#endif

#if TAK_USE_OLED
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #include <Wire.h>

  // The ttgo-lora32-v21new variant already defines these; fall back for boards
  // whose variant does not.
  #ifndef OLED_SDA
    #define OLED_SDA 21
  #endif
  #ifndef OLED_SCL
    #define OLED_SCL 22
  #endif

  // Do NOT drive GPIO 16 as the OLED reset on this board.
  //
  // The ttgo-lora32-v21new variant defines OLED_RST as 16, which is correct
  // only for the WROOM-based T3 v1.6. This board is an ESP32-PICO-D4: the SPI
  // flash die sits inside the package and its chip-select is GPIO 16.
  // display.begin() pulses the reset pin low for 10 ms, which fights the flash
  // controller for CS and leaves the die mid-transaction, so every later boot
  // hangs in an endless rst:0x8 (TG1WDT_SYS_RESET) loop that only a full power
  // cycle clears. The OLED on this revision has no reset line; -1 means none.
  constexpr int8_t OLED_RESET_PIN = -1;
  static Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET_PIN);
#endif

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint32_t reconnectDelayMs = RECONNECT_MIN_MS;
static uint32_t lastReportMs = 0;
static uint32_t lastConnectAttemptMs = 0;
static uint32_t reportCount = 0;
static String lastStatus = "boot";

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
static void drawScreen() {
#if TAK_USE_OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setCursor(0, 0);
  display.print(F("TAK "));
  display.print(TAK_CALLSIGN);

  display.setCursor(0, 12);
  display.print(F("wifi "));
  display.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--"));

  display.setCursor(0, 24);
  display.print(transport.name());
  display.print(' ');
  display.print(transport.connected() ? F("up") : F("down"));

  display.setCursor(0, 36);
  display.print(F("sent "));
  display.print(reportCount);

  display.setCursor(0, 48);
  display.print(lastStatus);

  display.display();
#endif
}

static void setStatus(const String& text) {
  lastStatus = text;
  Serial.println(text);
  drawScreen();
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------
// Hardcoded coordinates walking a slow circle, so a live feed is visibly
// different from a frozen one. Swapping in a real GPS means replacing this
// function body — nothing else in the firmware touches raw position.
static GeoPosition getPosition() {
  GeoPosition pos;
  pos.hae = HOME_HAE;
  pos.ce = cot::UNKNOWN_ACCURACY;
  pos.le = cot::UNKNOWN_ACCURACY;

  if (DRIFT_RADIUS_M <= 0.0) {
    pos.lat = HOME_LAT;
    pos.lon = HOME_LON;
    pos.speed = 0.0;
    pos.course = 0.0;
    return pos;
  }

  constexpr double METERS_PER_DEGREE_LAT = 111320.0;
  const double omega = TWO_PI / DRIFT_PERIOD_S;
  const double t = millis() / 1000.0;

  const double northOffset = DRIFT_RADIUS_M * sin(omega * t);
  const double eastOffset  = DRIFT_RADIUS_M * cos(omega * t);

  pos.lat = HOME_LAT + northOffset / METERS_PER_DEGREE_LAT;
  pos.lon = HOME_LON + eastOffset /
                           (METERS_PER_DEGREE_LAT * cos(radians(HOME_LAT)));

  // Velocity is the derivative of the circle, so the heading arrow on the map
  // points along the direction of travel instead of always north.
  const double northRate = DRIFT_RADIUS_M * omega * cos(omega * t);
  const double eastRate  = -DRIFT_RADIUS_M * omega * sin(omega * t);

  pos.speed = DRIFT_RADIUS_M * omega;
  double course = degrees(atan2(eastRate, northRate));
  if (course < 0.0) {
    course += 360.0;
  }
  pos.course = course;

  return pos;
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
static void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  setStatus(F("wifi connecting"));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    setStatus("wifi " + WiFi.localIP().toString());
  } else {
    setStatus(F("wifi FAILED"));
  }
}

// CoT timestamps are absolute UTC. Sending before NTP has run produces
// messages dated 1970 that are stale on arrival: the server accepts them
// without complaint and nothing ever appears on the map.
static void syncClock() {
  setStatus(F("ntp sync"));
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  const uint32_t start = millis();
  while (!cot::clockIsSane() && millis() - start < 30000) {
    delay(250);
  }

  if (cot::clockIsSane()) {
    setStatus("utc " + cot::isoTimestamp(cot::nowMillis()));
  } else {
    setStatus(F("ntp FAILED"));
  }
}

static void ensureConnected() {
  if (transport.connected()) {
    return;
  }
  if (millis() - lastConnectAttemptMs < reconnectDelayMs) {
    return;
  }
  lastConnectAttemptMs = millis();

  setStatus(String(transport.name()) + " dialing");
  if (transport.connect(TAK_HOST, TAK_PORT)) {
    reconnectDelayMs = RECONNECT_MIN_MS;
    setStatus(F("connected"));
  } else {
    // Back off so a server that is down does not turn into a connect storm.
    reconnectDelayMs = min(reconnectDelayMs * 2, RECONNECT_MAX_MS);
    setStatus("connect failed, retry " + String(reconnectDelayMs / 1000) + "s");
  }
}

// The server sends other clients' positions and ping responses back down the
// same stream. Nothing consumes them yet, but they must be drained or the
// socket buffer fills and the connection stalls.
static void drainIncoming() {
  uint8_t buf[256];
  int total = 0;
  int n;
  while ((n = transport.read(buf, sizeof(buf))) > 0) {
    total += n;
  }
  if (total > 0) {
    Serial.printf("RX %d bytes from server\n", total);
  }
}

static void sendPli() {
  if (!cot::clockIsSane()) {
    setStatus(F("clock unset, not sending"));
    return;
  }

  const GeoPosition pos = getPosition();
  const uint32_t staleSeconds =
      (REPORT_INTERVAL_MS / 1000) * STALE_MULTIPLIER;
  const String xml = cot::buildPli(pos, staleSeconds);

  if (transport.send(xml)) {
    reportCount++;
    Serial.printf("TX PLI #%lu  lat=%.7f lon=%.7f course=%.1f\n",
                  (unsigned long)reportCount, pos.lat, pos.lon, pos.course);
    drawScreen();
  } else {
    setStatus(F("send failed"));
    transport.stop();
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

#if TAK_USE_OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed (continuing headless)."));
  }
#endif

  Serial.println();
  Serial.printf("TAK client  uid=%s  callsign=%s\n", cot::deviceUid(), TAK_CALLSIGN);
  Serial.printf("target %s:%u via %s\n", TAK_HOST, (unsigned)TAK_PORT, transport.name());

  drawScreen();

  ensureWifi();
  syncClock();
  transport.begin();
}

void loop() {
  ensureWifi();

  if (WiFi.status() != WL_CONNECTED) {
    delay(500);
    return;
  }

  ensureConnected();

  if (transport.connected()) {
    drainIncoming();

    if (millis() - lastReportMs >= REPORT_INTERVAL_MS) {
      lastReportMs = millis();
      sendPli();
    }
  }

  delay(50);
}
