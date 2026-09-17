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
#include "replay_guard.h"
#include "settings.h"
#include "state_task.h"
#include "ui_task.h"

namespace {

QueueHandle_t txQueue = nullptr;
bool ready = false;

replay_guard_t replay;
uint16_t lastControllerId = 0;
uint16_t txSequence = 0;

// Captured when a packet is received, so the net task can report it without
// touching the driver.
int lastRssi = 0;

bool send(uint8_t type, const uint8_t *payload, uint8_t length) {
  frame_t frame = {};
  frame.ver = FRAME_VERSION;
  frame.type = type;
  frame.src = settings.nodeId;
  frame.seq = txSequence++;
  frame.len = length;
  memcpy(frame.payload, payload, length);

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));
  if (written <= 0) return false;

  if (LoRa.beginPacket() == 0) return false;
  LoRa.write(wire, (size_t)written);
  LoRa.endPacket(); // blocks until the packet is out

  // Back to listening. Forgetting this is the classic way to build a node that
  // answers the first command and then goes deaf.
  LoRa.receive();
  return true;
}

void sendAck(const RadioTx &message) {
  payload_cmd_ack_t ack = {};
  ack.counter = message.counter;
  ack.accepted = message.accepted;
  ack.state = message.state;
  ack.reason = message.reason;

  uint8_t payload[PAYLOAD_CMD_ACK_SIZE];
  const size_t length = payload_cmd_ack_encode(&ack, payload, sizeof(payload));
  if (length == 0) return;

  if (send(MSG_CMD_ACK, payload, (uint8_t)length)) {
    LOG_DEBUG(TagRadio, CodeAckTx, (int32_t)message.state);
  }
}

void sendAnnounce(const RadioTx &message) {
  payload_state_t announcement = {};
  // Before the first command there is nobody in particular to tell, so the
  // announcement goes to everyone. A controller that has just been switched on
  // then learns the node's state without having to command it first.
  announcement.dst = message.dst == 0 ? NODE_ID_BROADCAST : message.dst;
  announcement.state = message.state;
  announcement.cause = message.cause;

  uint8_t payload[PAYLOAD_STATE_SIZE];
  const size_t length = payload_state_encode(&announcement, payload, sizeof(payload));
  if (length == 0) return;

  // Repeated rather than acknowledged: a single unacknowledged frame is a coin
  // toss, and an ACK path for an announcement would double the protocol to
  // improve a fallback. The real backstop is the next command's ACK, which
  // carries the true state anyway.
  for (uint8_t repeat = 0; repeat < StateAnnounceRepeats; repeat++) {
    if (send(MSG_STATE, payload, (uint8_t)length)) {
      LOG_DEBUG(TagRadio, CodeAnnounceTx, repeat);
    }
    if (repeat + 1 < StateAnnounceRepeats) {
      vTaskDelay(pdMS_TO_TICKS(StateAnnounceIntervalMs));
    }
  }
}

void handleCommandFrame(const frame_t &frame) {
  payload_cmd_t command;
  if (payload_cmd_decode(frame.payload, frame.len, &command) == 0) {
    LOG_WARN(TagRadio, CodeRxBadFrame, frame.type);
    return;
  }

  // Not for us. Silently ignored - not even a refusal, because an answer of any
  // kind would tell the sender its command had reached one particular node.
  if (command.dst != settings.nodeId && command.dst != NODE_ID_BROADCAST) {
    LOG_DEBUG(TagRadio, CodeCmdNotForUs, (int32_t)command.dst);
    return;
  }

  if (!command_is_valid(command.command)) {
    LOG_WARN(TagRadio, CodeRxBadFrame, command.command);
    return;
  }

  LOG_DEBUG(TagRadio, CodeCmdRx, (int32_t)command.command);

  // First time we have heard from this controller since boot: seed the guard
  // from NVS. Without this the table starts empty after every reboot, and a
  // frame recorded before the reboot would be accepted as new - which would
  // make power-cycling the node the way to defeat the replay check.
  if (replay_last_counter(&replay, frame.src) == 0) {
    const uint32_t stored = settingsLoadReplayCounter(frame.src);
    if (stored > 0) replay_accept(&replay, frame.src, stored);
  }

  // The CRC proves the frame is intact, not that it is new. Without this check
  // a FIRE frame recorded off the air can be transmitted again and every other
  // check would pass, because it really did come from the controller once.
  if (!replay_accept(&replay, frame.src, command.counter)) {
    LOG_WARN(TagRadio, CodeCmdReplay, (int32_t)command.counter);

    // Answered rather than ignored. The commonest cause of a repeated counter
    // is not an attack, it is a controller retransmitting because our ACK was
    // lost - and staying silent would make it retry until it gave up and
    // reported LOST for a command the node had already carried out.
    RadioTx refusal = {};
    refusal.kind = TxAck;
    refusal.dst = frame.src;
    refusal.counter = command.counter;
    refusal.accepted = 0;
    refusal.state = stateCurrent();
    refusal.reason = REASON_REPLAY;
    radioPost(refusal);
    return;
  }

  settingsSaveReplayCounter(frame.src, command.counter);
  lastControllerId = frame.src;

  CommandRequest request = {};
  request.command = command.command;
  request.source = (uint8_t)SOURCE_LORA;
  request.controllerId = frame.src;
  request.counter = command.counter;

  stateSubmitCommand(request);
}

void receive() {
  const int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  lastRssi = LoRa.packetRssi();

  uint8_t wire[FRAME_MAX_SIZE];
  size_t length = 0;

  while (LoRa.available() && length < sizeof(wire)) {
    wire[length++] = (uint8_t)LoRa.read();
  }

  // Drain anything past our buffer, or the next parsePacket starts mid-frame.
  while (LoRa.available()) LoRa.read();

  frame_t frame;
  const frame_result_t result = frame_decode(wire, length, &frame);

  if (result != FRAME_OK) {
    LOG_WARN(TagRadio, CodeRxBadFrame, (int32_t)result);
    return;
  }

  // A type this build does not handle is not an error: the neighbouring
  // lora-queue-release nodes share this air and this envelope, and their
  // traffic is simply not ours.
  if (frame.type == MSG_CMD) handleCommandFrame(frame);
}

void radioTask(void *) {
  LoRa.receive();

  for (;;) {
    receive();

    // Transmitting takes the radio out of receive, so it is done here, in the
    // one task that owns it, rather than from whoever wanted to send.
    RadioTx message;
    while (xQueueReceive(txQueue, &message, 0) == pdTRUE) {
      if (message.kind == TxAck) {
        sendAck(message);
      } else {
        sendAnnounce(message);
      }
    }

    // Polling rather than the DIO0 interrupt: one task, one peripheral, no ISR
    // to get wrong, and at these data rates the latency is not the limit.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

} // namespace

void radioTaskStart() {
  replay_reset(&replay);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(LoraFrequencyHz)) {
    // A node that cannot hear the controller is still worth running: it can
    // still be commanded from the dashboard, and it can still report that its
    // radio is dead - which is how anyone finds out.
    ready = false;
    LOG_ERROR(TagRadio, CodeRadioInitFail, 0);
    uiSetRadioUp(false);
    return;
  }

  LoRa.setSpreadingFactor(LoraSpreadingFactor);
  LoRa.setSignalBandwidth(LoraSignalBandwidthHz);
  LoRa.setSyncWord(LoraSyncWord);
  LoRa.setTxPower(LoraTxPowerDbm);
  LoRa.enableCrc();

  ready = true;
  LOG_INFO(TagRadio, CodeRadioReady, 0);
  uiSetRadioUp(true);

  txQueue = xQueueCreate(RadioTxQueueDepth, sizeof(RadioTx));

  if (txQueue == nullptr ||
      xTaskCreate(radioTask, "radio", RadioTaskStack, nullptr, RadioTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

void radioPost(const RadioTx &message) {
  if (txQueue == nullptr) return;
  xQueueSend(txQueue, &message, 0);
}

uint16_t radioLastControllerId() { return lastControllerId; }

bool radioIsReady() { return ready; }

int radioLastRssi() { return lastRssi; }
