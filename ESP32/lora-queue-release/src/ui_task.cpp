#include "ui_task.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "board_pins.h"

// After this many RECEIVED symbols, the RX line clears and starts over.
constexpr int RX_CLEAR_AFTER = 16;

constexpr UBaseType_t UI_QUEUE_LENGTH = 8;

static Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET_PIN);

static QueueHandle_t uiQueue      = nullptr;
static TaskHandle_t  uiTaskHandle = nullptr;

// Task-owned state: only ever touched from uiTask() (or from uiShowFatal(),
// which suspends the task first).
static String txText  = "";   // symbols we have sent
static String rxText  = "";   // symbols we have received
static String status  = "";   // bottom line
static int    rxCount = 0;

static void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 0);  display.print("Node "); display.print(NODE_NAME);
  display.setCursor(0, 16); display.print("TX:");    display.print(txText);
  display.setCursor(0, 30); display.print("RX:");    display.print(rxText);
  display.setCursor(0, 50); display.print(status);
  display.display();
}

static void appendTrimmed(String& dst, char c, int maxLen = 17) {
  dst += c;
  if ((int)dst.length() > maxLen) dst = dst.substring(dst.length() - maxLen);
}

static void publish(const UiEvent& event) {
  if (!uiQueue) return;
  if (xQueueSend(uiQueue, &event, 0) != pdTRUE)
    Serial.println("uiQueue full - frame dropped");
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

    const uint32_t gotAtMs  = millis();
    const uint32_t sinceMs  = gotAtMs - event.stamp.keyedAtMs;

    switch (event.kind) {
      case UiEventKind::Banner:
        status = event.text;
        break;

      case UiEventKind::SymbolSent:
        appendTrimmed(txText, event.symbol);
        setSymbolStatus("sent", event.symbol, sinceMs);
        break;

      case UiEventKind::SymbolReceived:
        if (rxCount >= RX_CLEAR_AFTER) {   // reached the limit -> clear and restart
          rxText  = "";
          rxCount = 0;
        }
        rxText += event.symbol;
        rxCount++;
        setSymbolStatus("got", event.symbol, sinceMs);
        break;
    }

    drawScreen();

    if (event.kind == UiEventKind::Banner) continue;

    const uint32_t shownAtMs = millis();
    const char*    origin    = (event.kind == UiEventKind::SymbolReceived) ? "radio" : "key";
    Serial.printf("[ui]     %s '%c'  %s->ui %lu ms  draw %lu ms  %s->screen %lu ms",
                  (event.kind == UiEventKind::SymbolReceived) ? "RX" : "TX",
                  event.symbol, origin, (unsigned long)sinceMs,
                  (unsigned long)(shownAtMs - gotAtMs),
                  origin, (unsigned long)(shownAtMs - event.stamp.keyedAtMs));
    if (event.stamp.pressedAtMs)
      Serial.printf("  press->screen %lu ms",
                    (unsigned long)(shownAtMs - event.stamp.pressedAtMs));
    Serial.println();
  }
}

bool uiBegin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  return display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
}

bool uiTaskStart(UBaseType_t priority, BaseType_t core) {
  uiQueue = xQueueCreate(UI_QUEUE_LENGTH, sizeof(UiEvent));
  if (!uiQueue) return false;

  return xTaskCreatePinnedToCore(uiTask, "ui", 4096, nullptr, priority,
                                 &uiTaskHandle, core) == pdPASS;
}

void uiShowFatal(const char* message) {
  if (uiTaskHandle) vTaskSuspend(uiTaskHandle);   // we are taking the screen back

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(true);
  display.setCursor(0, 0);
  display.print(message);
  display.display();
}
