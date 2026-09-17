#include "state_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "config.h"
#include "event_buffer.h"
#include "frame.h"
#include "led_task.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"
#include "ui_task.h"

namespace {

QueueHandle_t stateQueue = nullptr;

// Owned by this task and read by nobody else. The two snapshot getters below
// are plain 32-bit reads, which are atomic on this core - they are for the
// shell and the health report, and nothing decides anything with them.
node_state_t current = STATE_BOOT;

// When the auto-arm is due, on the same clock as millis(). Meaningful only
// while `current` is a state that starts a countdown.
uint32_t countdownDeadlineMs = 0;

// The last command, for the screen.
uint8_t lastCommand = 0;
uint8_t lastSource = 0;
bool lastAccepted = true;
uint8_t lastReason = REASON_OK;

uint32_t countdownRemaining() {
  if (!state_starts_countdown(current)) return 0;

  // Signed difference, so this stays correct across the millis() wrap at 49
  // days. Comparing the unsigned values directly would make a node that has
  // been up that long arm itself instantly.
  const int32_t remaining = (int32_t)(countdownDeadlineMs - millis());
  return remaining <= 0 ? 0 : (uint32_t)remaining;
}

void publishUi() {
  UiUpdate update = {};
  update.state = (uint8_t)current;
  update.lastCommand = lastCommand;
  update.lastSource = lastSource;
  update.lastAccepted = lastAccepted;
  update.lastReason = lastReason;
  update.countdownMs = countdownRemaining();
  uiPost(update);
}

void publishLeds() {
  LedRequest request = {};
  request.state = (uint8_t)current;
  ledPost(request);
}

void recordEvent(node_state_t from, node_state_t to, const CommandRequest *command,
                 command_source_t source, bool accepted, reject_reason_t reason) {
  StateEventRecord record = {};
  record.timestampMs = millis();
  record.bootCount = settings.bootCount;
  record.fromState = (uint8_t)from;
  record.toState = (uint8_t)to;
  record.source = (uint8_t)source;
  record.accepted = accepted;
  record.reason = (uint8_t)reason;

  if (command != nullptr) {
    record.hasCommand = true;
    record.command = command->command;
    strncpy(record.commandId, command->commandId, COMMAND_ID_MAX - 1);
  }

  eventBufferPush(record);
}

// Enter a state. The only place `current` changes, and the only place the
// countdown is armed or cleared - so the two can never disagree.
void enter(node_state_t next) {
  const bool wasCounting = state_starts_countdown(current);
  current = next;

  if (state_starts_countdown(current)) {
    countdownDeadlineMs = millis() + settings.autoArmSeconds * 1000UL;
    LOG_INFO(TagState, CodeCountdownStarted, (int32_t)settings.autoArmSeconds);
  } else if (wasCounting) {
    LOG_INFO(TagState, CodeCountdownCancelled, 0);
  }

  LOG_INFO(TagState, CodeStateEnter, (int32_t)current);

  publishLeds();
  publishUi();
}

void applyCommand(const CommandRequest &request) {
  const node_state_t from = current;
  const command_t command = (command_t)request.command;
  const command_source_t source = (command_source_t)request.source;

  const transition_t result = state_apply(from, command);

  lastCommand = request.command;
  lastSource = request.source;
  lastAccepted = (result.kind != KIND_REJECT);
  lastReason = (uint8_t)result.reason;

  switch (result.kind) {
  case KIND_MOVE:
    LOG_INFO(TagState, CodeCmdAccepted,
             logPack((uint8_t)command, (uint8_t)result.state));
    enter(result.state);
    break;

  case KIND_RESTART:
    // INIT while already in INIT: accepted, the state does not change, and the
    // countdown starts again. The one no-op that does something - it is how an
    // operator holds a node in INIT while still setting up.
    countdownDeadlineMs = millis() + settings.autoArmSeconds * 1000UL;
    LOG_INFO(TagState, CodeCountdownRestarted, (int32_t)settings.autoArmSeconds);
    publishUi();
    break;

  case KIND_NOOP:
    // Accepted and nothing to do. The radio retries, so a lost ACK must not
    // turn a command that worked into one that failed.
    LOG_INFO(TagState, CodeCmdAccepted,
             logPack((uint8_t)command, (uint8_t)result.state));
    publishUi();
    break;

  case KIND_REJECT:
  default:
    LOG_WARN(TagState, CodeCmdRejected,
             logPack((uint8_t)command, (uint8_t)result.reason));
    publishUi();
    break;
  }

  recordEvent(from, result.state, &request, source, result.kind != KIND_REJECT,
              result.reason);

  // Every command off the radio is answered, accepted or not. The ACK carries
  // the resulting state, which is how a controller that has lost track
  // resynchronises rather than guessing.
  if (source == SOURCE_LORA) {
    RadioTx ack = {};
    ack.kind = TxAck;
    ack.dst = request.controllerId;
    ack.counter = request.counter;
    ack.accepted = (result.kind != KIND_REJECT) ? 1 : 0;
    ack.state = (uint8_t)result.state;
    ack.reason = (uint8_t)result.reason;
    radioPost(ack);
  }
}

void applyAutoArm() {
  const node_state_t from = current;
  const transition_t result = state_auto_arm(from);

  if (result.kind != KIND_MOVE) return; // not from this state; nothing to do

  LOG_WARN(TagState, CodeAutoArm, (int32_t)result.state);

  lastCommand = 0; // nothing commanded this
  lastSource = (uint8_t)SOURCE_TIMER;
  lastAccepted = true;
  lastReason = REASON_OK;

  enter(result.state);
  recordEvent(from, result.state, nullptr, SOURCE_TIMER, true, REASON_OK);

  // Tell whoever last commanded us. Without this the controller would still be
  // showing INIT after the node had armed itself, and the operator's next press
  // would send a pointless ARM instead of FIRE.
  RadioTx announce = {};
  announce.kind = TxAnnounce;
  announce.dst = radioLastControllerId();
  announce.state = (uint8_t)result.state;
  announce.cause = STATE_CAUSE_AUTO_ARM;
  radioPost(announce);
}

void stateTask(void *) {
  // A node boots into SAFE. Announced here rather than assumed, so the LEDs and
  // the screen agree with the state from the first moment.
  enter(STATE_BOOT);

  for (;;) {
    // The countdown is this receive's timeout. No timer object, no second thing
    // that can be running when the state says it should not be: in any state
    // but INIT the task simply blocks forever, and in INIT it blocks until the
    // deadline. A command wakes it early; nothing arriving IS the countdown
    // expiring.
    TickType_t wait = portMAX_DELAY;

    if (state_starts_countdown(current)) {
      const uint32_t remaining = countdownRemaining();
      wait = remaining == 0 ? 0 : pdMS_TO_TICKS(remaining);
    }

    CommandRequest request;

    if (xQueueReceive(stateQueue, &request, wait) == pdTRUE) {
      applyCommand(request);
    } else if (state_starts_countdown(current) && countdownRemaining() == 0) {
      applyAutoArm();
    }
  }
}

} // namespace

void stateTaskStart() {
  stateQueue = xQueueCreate(StateQueueDepth, sizeof(CommandRequest));

  if (stateQueue == nullptr ||
      xTaskCreate(stateTask, "state", StateTaskStack, nullptr, StateTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

bool stateSubmitCommand(const CommandRequest &request) {
  if (stateQueue == nullptr) return false;

  // Never blocks. A full queue means the state task has stopped, and blocking
  // the radio task on it would take the node down with it rather than leaving
  // it able to report the problem.
  return xQueueSend(stateQueue, &request, 0) == pdTRUE;
}

uint8_t stateCurrent() { return (uint8_t)current; }

uint32_t stateCountdownRemainingMs() { return countdownRemaining(); }
