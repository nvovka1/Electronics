#include "radio_task.h"

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "board_pins.h"
#include "config.h"
#include "frame.h"
#include "log.h"
#include "settings.h"

namespace {

QueueHandle_t rxQueue = nullptr;
bool ready = false;
uint16_t txSequence = 0;
int lastRssi = 0;
int txPowerDbm = LoraTxPowerDbm;

// Guards the driver between the command task's transmit and the radio task's
// receive. The one place in this firmware where two tasks touch the same thing,
// and the reason is that a synchronous send is much simpler to reason about
// than a send queue plus a completion notification.
SemaphoreHandle_t radioLock = nullptr;

void handleAck(const frame_t &frame) {
  payload_cmd_ack_t ack;
  if (payload_cmd_ack_decode(frame.payload, frame.len, &ack) == 0) {
    LOG_WARN(TagRadio, CodeRxBadFrame, frame.type);
    return;
  }

  RadioRx received = {};
  received.kind = RxAck;
  received.src = frame.src;
  received.counter = ack.counter;
  received.accepted = ack.accepted;
  received.state = ack.state;
  received.reason = ack.reason;

  xQueueSend(rxQueue, &received, 0);
}

void handleAnnounce(const frame_t &frame) {
  payload_state_t announcement;
  if (payload_state_decode(frame.payload, frame.len, &announcement) == 0) {
    LOG_WARN(TagRadio, CodeRxBadFrame, frame.type);
    return;
  }

  // Addressed to us or to everyone. A node announcing to a different controller
  // is not our business, and acting on it would put another operator's node on
  // our screen.
  if (announcement.dst != settings.nodeId && announcement.dst != NODE_ID_BROADCAST) return;

  RadioRx received = {};
  received.kind = RxAnnounce;
  received.src = frame.src;
  received.state = announcement.state;
  received.cause = announcement.cause;

  LOG_INFO(TagRadio, CodeAnnounceRx, (int32_t)announcement.state);

  xQueueSend(rxQueue, &received, 0);
}

void receive() {
  if (xSemaphoreTake(radioLock, 0) != pdTRUE) return;

  const int packetSize = LoRa.parsePacket();

  if (packetSize <= 0) {
    xSemaphoreGive(radioLock);
    return;
  }

  lastRssi = LoRa.packetRssi();

  uint8_t wire[FRAME_MAX_SIZE];
  size_t length = 0;

  while (LoRa.available() && length < sizeof(wire)) wire[length++] = (uint8_t)LoRa.read();
  while (LoRa.available()) LoRa.read(); // or the next parse starts mid-frame

  xSemaphoreGive(radioLock);

  frame_t frame;
  const frame_result_t result = frame_decode(wire, length, &frame);

  if (result != FRAME_OK) {
    LOG_WARN(TagRadio, CodeRxBadFrame, (int32_t)result);
    return;
  }

  // A type this build does not handle is not an error: the lora-queue-release
  // nodes share this air and this envelope, and their traffic is not ours.
  if (frame.type == MSG_CMD_ACK) handleAck(frame);
  else if (frame.type == MSG_STATE) handleAnnounce(frame);
}

void radioTask(void *) {
  for (;;) {
    receive();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

} // namespace

void radioTaskStart() {
  radioLock = xSemaphoreCreateMutex();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(LoraFrequencyHz)) {
    ready = false;
    LOG_ERROR(TagRadio, CodeRadioInitFail, 0);
    return;
  }

  // Every one of these must match the node. A controller on a different
  // spreading factor is a controller nothing hears, and nothing about the
  // silence says why.
  LoRa.setSpreadingFactor(LoraSpreadingFactor);
  LoRa.setSignalBandwidth(LoraSignalBandwidthHz);
  LoRa.setSyncWord(LoraSyncWord);
  LoRa.setTxPower(LoraTxPowerDbm);
  LoRa.enableCrc();
  LoRa.receive();

  ready = true;
  LOG_INFO(TagRadio, CodeRadioReady, 0);

  rxQueue = xQueueCreate(RadioRxQueueDepth, sizeof(RadioRx));

  if (rxQueue == nullptr ||
      xTaskCreate(radioTask, "radio", RadioTaskStack, nullptr, RadioTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

bool radioIsReady() { return ready; }

bool radioSendCommand(uint16_t destination, uint8_t command, uint32_t counter) {
  if (!ready) return false;

  payload_cmd_t payload = {};
  payload.dst = destination;
  payload.command = command;
  payload.counter = counter;

  frame_t frame = {};
  frame.ver = FRAME_VERSION;
  frame.type = MSG_CMD;
  frame.src = settings.nodeId;
  frame.seq = txSequence++;
  frame.len = (uint8_t)payload_cmd_encode(&payload, frame.payload, sizeof(frame.payload));

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));
  if (written <= 0) return false;

  xSemaphoreTake(radioLock, portMAX_DELAY);

  const bool sent = LoRa.beginPacket() != 0;
  if (sent) {
    LoRa.write(wire, (size_t)written);
    LoRa.endPacket(); // blocks until the packet is out
  }

  // Back to listening, or the ACK we are about to wait for never arrives.
  LoRa.receive();

  xSemaphoreGive(radioLock);

  if (sent) LOG_INFO(TagRadio, CodeCmdTx, (int32_t)command);
  return sent;
}

bool radioWait(RadioRx *out, uint32_t timeoutMs) {
  if (rxQueue == nullptr || out == nullptr) return false;
  return xQueueReceive(rxQueue, out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void radioDrain() {
  if (rxQueue == nullptr) return;

  RadioRx discarded;
  while (xQueueReceive(rxQueue, &discarded, 0) == pdTRUE) {
  }
}

int radioLastRssi() { return lastRssi; }

void radioSetTxPower(int dbm) {
  if (dbm < 2) dbm = 2;   // PA_BOOST cannot go lower; the library clamps anyway
  if (dbm > 17) dbm = 17; // above this needs the high-power register dance

  // Through the lock: this writes the radio's registers, and doing that while
  // the task is mid-parse is how a driver ends up in a state nobody can
  // reproduce.
  xSemaphoreTake(radioLock, portMAX_DELAY);
  LoRa.setTxPower(dbm);
  LoRa.receive();
  xSemaphoreGive(radioLock);

  txPowerDbm = dbm;
}

int radioTxPower() { return txPowerDbm; }
