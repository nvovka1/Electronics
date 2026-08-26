#include "radio_task.h"

#include <SPI.h>
#include <LoRa.h>

#include "app_events.h"
#include "board_pins.h"
#include "tone.h"
#include "ui_task.h"

// How long the task blocks on its queue before polling the receiver again.
constexpr uint32_t RADIO_POLL_MS = 20;

constexpr UBaseType_t RADIO_QUEUE_LENGTH = 8;

static QueueHandle_t radioQueue      = nullptr;
static TaskHandle_t  radioTaskHandle = nullptr;

static void transmitSymbol(char symbol) {
  LoRa.beginPacket();
  LoRa.write((uint8_t)symbol);
  LoRa.endPacket();
  LoRa.receive();                    // straight back to listening

  Serial.printf("[radio] TX symbol: '%c'\n", symbol);
  uiPostSymbolSent(symbol);
  toneBeepSymbol(symbol);
}

static void receivePending() {
  if (LoRa.parsePacket() <= 0) return;

  while (LoRa.available()) {
    const char symbol = (char)LoRa.read();
    Serial.printf("[radio] RX symbol: '%c'  (RSSI %d)\n", symbol, LoRa.packetRssi());
    uiPostSymbolReceived(symbol);
    toneBeepSymbol(symbol);
  }
  LoRa.receive();
}

static void radioTask(void* /*arg*/) {
  for (;;) {
    RadioRequest request;
    if (xQueueReceive(radioQueue, &request, pdMS_TO_TICKS(RADIO_POLL_MS)) == pdTRUE)
      transmitSymbol(request.symbol);

    receivePending();
  }
}

bool radioBegin() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_FREQ)) return false;

  LoRa.receive();
  return true;
}

bool radioTaskStart(UBaseType_t priority, BaseType_t core) {
  radioQueue = xQueueCreate(RADIO_QUEUE_LENGTH, sizeof(RadioRequest));
  if (!radioQueue) return false;

  return xTaskCreatePinnedToCore(radioTask, "radio", 4096, nullptr, priority,
                                 &radioTaskHandle, core) == pdPASS;
}

bool radioSendSymbol(char symbol, TickType_t timeout) {
  if (!radioQueue) return false;
  RadioRequest request{symbol};
  return xQueueSend(radioQueue, &request, timeout) == pdTRUE;
}
