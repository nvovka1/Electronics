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

static void transmitSymbol(char symbol, const EventStamp& stamp) {
  const uint32_t startedAtMs = millis();

  LoRa.beginPacket();
  LoRa.write((uint8_t)symbol);
  LoRa.endPacket();                  // blocks until the packet is on the air
  LoRa.receive();                    // straight back to listening

  const uint32_t sentAtMs = millis();
  uiPostSymbolSent(symbol, stamp);   // hand the screen over before logging

  Serial.printf("[radio]  TX '%c'  key->tx %lu ms  air %lu ms  key->sent %lu ms"
                "  press->sent %lu ms\n",
                symbol,
                (unsigned long)(startedAtMs - stamp.keyedAtMs),
                (unsigned long)(sentAtMs - startedAtMs),
                (unsigned long)(sentAtMs - stamp.keyedAtMs),
                (unsigned long)(sentAtMs - stamp.pressedAtMs));
  toneBeepSymbol(symbol);
}

static void receivePending() {
  if (LoRa.parsePacket() <= 0) return;

  while (LoRa.available()) {
    const char     symbol   = (char)LoRa.read();
    const uint32_t readAtMs = millis();
    // No local key press behind this one: the radio read is the origin.
    uiPostSymbolReceived(symbol, readAtMs);

    Serial.printf("[radio]  RX '%c'  read@%lu ms  RSSI %d\n",
                  symbol, (unsigned long)readAtMs, LoRa.packetRssi());
    toneBeepSymbol(symbol);
  }
  LoRa.receive();
}

static void radioTask(void* /*arg*/) {
  for (;;) {
    RadioRequest request;
    if (xQueueReceive(radioQueue, &request, pdMS_TO_TICKS(RADIO_POLL_MS)) == pdTRUE)
      transmitSymbol(request.symbol, request.stamp);

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

bool radioSendSymbol(char symbol, const EventStamp& stamp, TickType_t timeout) {
  if (!radioQueue) return false;
  const RadioRequest request{symbol, stamp};
  return xQueueSend(radioQueue, &request, timeout) == pdTRUE;
}
