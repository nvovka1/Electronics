#include "tasks/ui_task.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "app/safe_mode.h"
#include "core/calib.h"
#include "core/config.h"
#include "core/log.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "hal/board_pins.h"
#include "net/net_task.h"
#include "net/ota.h"

// After this many RECEIVED symbols, the RX line clears and starts over.
constexpr int RX_CLEAR_AFTER = 16;

constexpr UBaseType_t UI_QUEUE_LENGTH = 8;

constexpr uint8_t SSD1306_I2C_ADDR = 0x3C;

// OLED_RESET_PIN is -1 on purpose and must stay that way. The board variant
// header calls GPIO16 the display reset, but these modules are PICO-D4, where
// GPIO16 is the chip select of the embedded flash. Driving it wedges the boot
// into a silent watchdog loop that looks exactly like a dead board.
static Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET_PIN);

static QueueHandle_t uiQueue = nullptr;
static TaskHandle_t uiTaskHandle = nullptr;

// The UI task owns I2C, but the POST has to probe the panel and `self-test`
// re-runs the POST from the shell task.
static SemaphoreHandle_t i2cMutex = nullptr;

static bool i2cTake(TickType_t timeout) {
  if (!i2cMutex) return true;   // setup() is still single-threaded
  return xSemaphoreTake(i2cMutex, timeout) == pdTRUE;
}

static void i2cGive() {
  if (i2cMutex) xSemaphoreGive(i2cMutex);
}

// Task-owned state: only ever touched from uiTask().
static String txText = "";   // symbols we have sent
static String rxText = "";   // symbols we have received
static String status = "";   // bottom line
static int rxCount = 0;
static bool showingInfo = false;

// --- drawing --------------------------------------------------------------

static void drawBattery(int16_t x, int16_t y) {
  if (!batteryTrusted()) {
    display.setCursor(x, y);
    display.print("VBAT ?");
    return;
  }
  const uint8_t dv = batteryDeciVolts();
  display.setCursor(x, y);
  display.printf("%u.%uV", dv / 10, dv % 10);
}

// Identity, build and health on one screen. Everything an operator needs to
// answer "which node is this and what is on it" without a laptop.
static void drawInfoPage() {
  const uint16_t mask = postMask();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setCursor(0, 0);
  display.printf("%s  node %u", calibSerial(), config().node_id);

  display.setCursor(0, 12);
  display.printf("fw %s", FW_SEMVER);
  display.setCursor(0, 22);
  display.printf("%s %s", FW_GIT_HASH, FW_GIT_DIRTY ? "DIRTY" : FW_BUILD_TYPE);

  display.setCursor(0, 34);
  display.printf("POST 0x%04X %s", mask, mask ? "FAIL" : "OK");

  display.setCursor(74, 12);
  if (otaIsOnTrial())
    display.print("TRIAL");
  else if (netUpdateAvailable())
    display.print("UPD!");
  else if (netIsConnected())
    display.print("WiFi");
  else
    display.print("----");

  display.setCursor(74, 22);
  if (netLastCheckInMs())
    display.printf("%lus", (unsigned long)((millis() - netLastCheckInMs()) / 1000u));
  else
    display.print("no rpt");

  display.setCursor(0, 44);
  display.printf("up %lus  rb %lu", (unsigned long)(millis() / 1000u),
                 (unsigned long)totalBootCount());

  display.setCursor(0, 54);
  drawBattery(0, 54);
  if (safeModeActive()) {
    display.setCursor(64, 54);
    display.print("SAFE MODE");
  }

  display.display();
}

static void drawMainScreen() {
  const uint16_t mask = postMask();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // The header carries identity permanently. In a field of forty boxes,
  // "which one am I holding" must never be a guess.
  display.setCursor(0, 0);
  display.printf("%u %s", config().node_id, calibSerial());
  display.setCursor(74, 0);
  display.printf("v%s", FW_SEMVER);

  // One character for the uplink, in the corner, permanently. The three states
  // an operator cares about are "reporting", "an update is waiting" and "on
  // trial, about to be judged".
  display.setCursor(122, 0);
  if (otaIsOnTrial())
    display.print("T");
  else if (netUpdateAvailable())
    display.print("U");
  else if (netIsConnected())
    display.print("*");

  display.setCursor(0, 16);
  display.print("TX:");
  display.print(txText);
  display.setCursor(0, 30);
  display.print("RX:");
  display.print(rxText);

  display.setCursor(0, 50);
  display.print(status);

  // A failed POST stays on the screen for as long as it is true, rather than
  // scrolling away in a boot message nobody was watching.
  if (mask) {
    display.setCursor(80, 50);
    display.printf("P:%03X", mask);
  }

  display.display();
}

static void draw() {
  if (!i2cTake(pdMS_TO_TICKS(200))) return;   // a skipped frame beats a corrupt bus

  if (showingInfo)
    drawInfoPage();
  else
    drawMainScreen();

  i2cGive();
}

static void appendTrimmed(String& dst, char c, int maxLen = 17) {
  dst += c;
  if ((int)dst.length() > maxLen) dst = dst.substring(dst.length() - maxLen);
}

// --- posting --------------------------------------------------------------

static void publish(const UiEvent& event) {
  if (!uiQueue) return;
  if (xQueueSend(uiQueue, &event, 0) != pdTRUE) LOG_W(TAG_UI, E_QUEUE_FULL, 1);
}

void uiPostBanner(const char* text) {
  UiEvent event{UiEventKind::Banner, 0, "", EventStamp{0, 0}};
  strlcpy(event.text, text, sizeof(event.text));
  publish(event);
}

void uiPostSymbolSent(char symbol, const EventStamp& stamp) {
  publish(UiEvent{UiEventKind::SymbolSent, symbol, "", stamp});
}

void uiPostSymbolReceived(char symbol, uint32_t radioAtMs) {
  publish(UiEvent{UiEventKind::SymbolReceived, symbol, "", EventStamp{0, radioAtMs}});
}

void uiShowInfoPage() {
  publish(UiEvent{UiEventKind::ShowInfo, 0, "", EventStamp{0, 0}});
}

void uiShowMainPage() {
  publish(UiEvent{UiEventKind::ShowMain, 0, "", EventStamp{0, 0}});
}

void uiToggleInfoPage() {
  publish(UiEvent{showingInfo ? UiEventKind::ShowMain : UiEventKind::ShowInfo, 0, "",
                  EventStamp{0, 0}});
}

void uiRefresh() { publish(UiEvent{UiEventKind::Refresh, 0, "", EventStamp{0, 0}}); }

// The status line doubles as a latency readout: how long the symbol took from
// the key (or, for a received one, from the radio) to reaching this task.
static void setSymbolStatus(const char* verb, char symbol, uint32_t sinceMs) {
  char line[22];
  snprintf(line, sizeof(line), "%s %c +%lu ms", verb, symbol, (unsigned long)sinceMs);
  status = line;
}

static void uiTask(void* /*arg*/) {
  for (;;) {
    UiEvent event;
    if (xQueueReceive(uiQueue, &event, portMAX_DELAY) != pdTRUE) continue;

    const uint32_t gotAtMs = millis();
    const uint32_t sinceMs = gotAtMs - event.stamp.keyedAtMs;

    switch (event.kind) {
      case UiEventKind::Banner:
        status = event.text;
        showingInfo = false;
        break;

      case UiEventKind::SymbolSent:
        appendTrimmed(txText, event.symbol);
        setSymbolStatus("sent", event.symbol, sinceMs);
        showingInfo = false;
        break;

      case UiEventKind::SymbolReceived:
        if (rxCount >= RX_CLEAR_AFTER) {   // reached the limit -> clear and restart
          rxText = "";
          rxCount = 0;
        }
        rxText += event.symbol;
        rxCount++;
        setSymbolStatus("got", event.symbol, sinceMs);
        showingInfo = false;
        break;

      case UiEventKind::ShowInfo:
        showingInfo = true;
        break;

      case UiEventKind::ShowMain:
        showingInfo = false;
        break;

      case UiEventKind::Refresh:
        break;
    }

    draw();

    if (event.kind != UiEventKind::SymbolSent &&
        event.kind != UiEventKind::SymbolReceived)
      continue;

    LOG_T(TAG_UI, E_NONE, millis() - gotAtMs);
  }
}

// --- lifecycle ------------------------------------------------------------

bool uiBegin() {
  if (!i2cMutex) i2cMutex = xSemaphoreCreateMutex();

  Wire.begin(OLED_SDA, OLED_SCL);
  return display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
}

void uiShowOtaProgress(const char *toVersion, uint8_t percent) {
  if (!i2cTake(pdMS_TO_TICKS(500))) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setCursor(0, 0);
  display.printf("node %u  %s", config().node_id, calibSerial());

  display.setCursor(0, 14);
  display.printf("%s -> %s", FW_SEMVER, toVersion ? toVersion : "?");

  display.setCursor(0, 26);
  display.print("UPDATING - keep power");

  // A bar, because a number alone does not show a download that has stopped
  // moving and this one takes minutes.
  display.drawRect(0, 38, 128, 10, SSD1306_WHITE);
  const int16_t filled = (int16_t)((int32_t)percent * 126 / 100);
  if (filled > 0) display.fillRect(1, 39, filled, 8, SSD1306_WHITE);

  display.setCursor(0, 54);
  display.printf("%3u%%", percent);

  // The battery stays on this screen for the whole download. It is the number
  // that decides whether this ends in a new image or in a node that has to be
  // collected.
  drawBattery(92, 54);

  display.display();
  i2cGive();
}

void uiSuspendPanel() {
  if (!i2cTake(pdMS_TO_TICKS(200))) return;
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  i2cGive();
}

void uiResumePanel() {
  if (!i2cTake(pdMS_TO_TICKS(200))) return;
  display.ssd1306_command(SSD1306_DISPLAYON);
  i2cGive();
}

bool uiProbePanel() {
  if (!i2cTake(pdMS_TO_TICKS(200))) return false;

  Wire.beginTransmission(SSD1306_I2C_ADDR);
  const bool acked = (Wire.endTransmission() == 0);

  i2cGive();
  return acked;
}

void uiDrawSplash() {
  showingInfo = true;
  draw();
  showingInfo = false;
}

bool uiTaskStart(UBaseType_t priority, BaseType_t core) {
  uiQueue = xQueueCreate(UI_QUEUE_LENGTH, sizeof(UiEvent));
  if (!uiQueue) return false;

  return xTaskCreatePinnedToCore(uiTask, "ui", 4096, nullptr, priority,
                                 &uiTaskHandle, core) == pdPASS;
}
