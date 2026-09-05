#include "tasks/tone.h"

#include "hal/board_pins.h"

// --- Morse sidetone lengths ---
constexpr uint32_t DOT_MS  = 120;
constexpr uint32_t DASH_MS = 360;

constexpr UBaseType_t TONE_QUEUE_LENGTH = 8;

static SemaphoreHandle_t toneMutex      = nullptr;
static QueueHandle_t     toneQueue      = nullptr;
static TaskHandle_t      toneTaskHandle = nullptr;

static void drive(bool on) {
  digitalWrite(LED_PIN,    on ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
}

bool toneBegin() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  drive(false);

  toneMutex = xSemaphoreCreateMutex();
  return toneMutex != nullptr;
}

bool toneTryStart() {
  if (!toneMutex || xSemaphoreTake(toneMutex, 0) != pdTRUE) return false;
  drive(true);
  return true;
}

void toneStop() {
  drive(false);
  xSemaphoreGive(toneMutex);
}

static void beep(char symbol) {
  if (!toneTryStart()) return;   // the key is down: its live sidetone wins
  vTaskDelay(pdMS_TO_TICKS(symbol == '-' ? DASH_MS : DOT_MS));
  toneStop();
}

static void toneTask(void* /*arg*/) {
  for (;;) {
    char symbol = 0;
    if (xQueueReceive(toneQueue, &symbol, portMAX_DELAY) == pdTRUE) beep(symbol);
  }
}

bool toneTaskStart(UBaseType_t priority, BaseType_t core) {
  toneQueue = xQueueCreate(TONE_QUEUE_LENGTH, sizeof(char));
  if (!toneQueue) return false;

  return xTaskCreatePinnedToCore(toneTask, "tone", 2048, nullptr, priority,
                                 &toneTaskHandle, core) == pdPASS;
}

void toneRequest(char symbol) {
  if (!toneQueue) return;
  xQueueSend(toneQueue, &symbol, 0);
}
