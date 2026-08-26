#include "tone.h"

#include "board_pins.h"

// --- Morse sidetone lengths ---
constexpr uint32_t DOT_MS  = 120;
constexpr uint32_t DASH_MS = 360;

static SemaphoreHandle_t toneMutex = nullptr;

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

void toneBeepSymbol(char symbol) {
  if (!toneTryStart()) return;
  vTaskDelay(pdMS_TO_TICKS(symbol == '-' ? DASH_MS : DOT_MS));
  toneStop();
}
