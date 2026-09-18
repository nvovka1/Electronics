#include "command_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "button_task.h"
#include "config.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"
#include "ui_task.h"

namespace {

// What we think the targeted node is doing. Never restored from NVS: a
// controller that comes back believing something about a node it has not spoken
// to since is worse than one that admits it does not know.
node_state_t believed = STATE_SAFE;
belief_t belief = BELIEF_UNKNOWN;

uint8_t lastCommand = COMMAND_NONE;
uint8_t lastResult = ResultNone;
uint8_t lastReason = REASON_OK;
uint8_t lastAttempts = 0;
bool lastWasAnnounced = false;

void publishUi() {
  UiUpdate update = {};
  update.targetId = settings.targetId;
  update.believedState = (uint8_t)believed;
  update.belief = (uint8_t)belief;
  update.lastCommand = lastCommand;
  update.result = lastResult;
  update.reason = lastReason;
  update.attempts = lastAttempts;
  update.rssi = (int16_t)radioLastRssi();
  update.announced = lastWasAnnounced;
  uiPost(update);
}

// The only place the belief changes.
void adoptState(uint8_t state, bool announced) {
  if (!state_is_valid(state)) return;

  believed = (node_state_t)state;
  belief = BELIEF_KNOWN;
  lastWasAnnounced = announced;
}

// Sends one command and waits for its answer, retrying on silence.
//
// Returns having updated the belief if - and only if - the node answered.
void sendCommand(command_t command) {
  const uint32_t counter = counterNext();

  lastCommand = (uint8_t)command;
  lastReason = REASON_OK;
  lastWasAnnounced = false;

  // Once, before the first attempt. Anything queued now predates this command
  // and can only confuse the wait.
  radioDrain();

  for (uint8_t attempt = 1; attempt <= SendAttempts; attempt++) {
    // SAFE has been pressed. Abandon whatever this was and let it through -
    // making somebody wait two seconds for a failing FIRE to run out of
    // retries before SAFE is even sent is the wrong way round. The press is
    // still on the queue, so the loop picks it up as soon as this returns.
    //
    // Not checked for a SAFE command itself, or it would abandon itself.
    if (command != COMMAND_SAFE && buttonSafePending()) {
      // Logged, and at WARN. Without this the command simply stops appearing in
      // the log part way through its retries, which reads as a firmware fault
      // rather than as the one thing the operator asked for.
      LOG_WARN(TagButton, CodeSafeAbort, (int32_t)command);

      lastResult = ResultLost;
      belief = BELIEF_LOST;
      publishUi();
      return;
    }

    lastAttempts = attempt;

    // NOT drained here, only before the first attempt. Draining between
    // attempts throws away an ACK that arrived a little after its deadline -
    // and that ACK is for this same counter, so it would have answered the
    // command. Discarding it guarantees the retry, and the retry is then
    // refused by the node as a replay. The counter check below is what keeps a
    // genuinely stale ACK out; the drain was doing harm on top of it.

    if (!radioSendCommand(settings.targetId, (uint8_t)command, counter)) {
      // The radio never came up. Worth its own line: it looks identical to "the
      // node is not answering" on the screen, and it is a completely different
      // fault.
      LOG_ERROR(TagRadio, CodeRadioBusy, (int32_t)command);
      lastResult = ResultLost;
      belief = BELIEF_LOST;
      publishUi();
      return;
    }

    publishUi(); // show the attempt going out

    // Wait for an answer. Anything that is not the ACK for this counter is
    // handled and then waited past, rather than ending the attempt: an
    // announcement can arrive at any moment and is still worth believing.
    const uint32_t deadline = millis() + AckTimeoutMs;

    for (;;) {
      const int32_t remaining = (int32_t)(deadline - millis());
      if (remaining <= 0) break;

      RadioRx received;
      if (!radioWait(&received, (uint32_t)remaining)) break;

      if (received.kind == RxAnnounce) {
        adoptState(received.state, true);
        publishUi();
        continue;
      }

      // An ACK for a different command - almost always one we gave up on -
      // still carries the node's state, so it is worth believing even though it
      // does not end this wait.
      if (received.counter != counter) {
        LOG_DEBUG(TagRadio, CodeAckIgnored, (int32_t)received.counter);
        adoptState(received.state, false);
        continue;
      }

      // The answer. The node's state comes back whether or not it obeyed, which
      // is what makes a refusal useful rather than just a failure.
      adoptState(received.state, false);
      lastReason = received.reason;
      lastResult = received.accepted ? ResultAccepted : ResultRefused;

      if (received.accepted) {
        LOG_INFO(TagRadio, CodeCmdAccepted, (int32_t)received.state);
      } else {
        LOG_WARN(TagRadio, CodeCmdRefused, (int32_t)received.reason);
      }

      publishUi();
      return;
    }

    LOG_WARN(TagRadio, CodeTxNoAck, attempt);
  }

  // Out of attempts. The belief is left exactly as it was and marked stale
  // rather than being guessed forward: the command may well have arrived and
  // only the ACK been lost, so the node could be in either state. The next
  // command's ACK settles it.
  LOG_WARN(TagRadio, CodeCmdLost, SendAttempts);

  lastResult = ResultLost;
  belief = BELIEF_LOST;
  publishUi();
}

void handleSequenceButton() {
  const command_t next = sequence_next(believed, belief);

  if (next == COMMAND_NONE) {
    // FIRE, and the node is latched there. Nothing follows, and SAFE has its
    // own button - so this says so instead of sending something refusable.
    LOG_INFO(TagButton, CodeNothingToDo, (int32_t)believed);
    lastCommand = COMMAND_NONE;
    lastResult = ResultNothingToDo;
    publishUi();
    return;
  }

  sendCommand(next);
}

void handleTargetButton() {
  uint16_t next = (uint16_t)(settings.targetId + 1);
  if (next > settings.maxTargetId) next = 1;

  settingsSaveTargetId(next);

  // INFO, not DEBUG. Re-aiming a controller is not a small thing: after it,
  // every command goes somewhere else, and a node that is not the target hears
  // each one and says nothing - which reads as a dead link. This line is what
  // tells you the link was never the problem.
  LOG_INFO(TagCfg, CodeTargetChanged, next);

  // A different node, so everything believed about the last one is now about
  // the wrong device. Saying "unknown" is the honest answer, and the next
  // command's ACK fills it in.
  believed = STATE_SAFE;
  belief = BELIEF_UNKNOWN;
  lastCommand = COMMAND_NONE;
  lastResult = ResultNone;
  lastAttempts = 0;

  publishUi();
}

void commandTask(void *) {
  publishUi();

  for (;;) {
    ButtonEvent event;

    // A timeout rather than a blocking wait, so announcements arriving while
    // nobody is pressing anything still reach the screen. This is how the
    // controller finds out that a node armed itself.
    if (!buttonWait(&event, UiRefreshMs)) {
      RadioRx received;
      while (radioWait(&received, 0)) {
        if (received.kind == RxAnnounce) {
          adoptState(received.state, true);
          publishUi();
        }
      }
      continue;
    }

    switch (event.button) {
    case ButtonSequence:
      handleSequenceButton();
      break;

    case ButtonSafe:
      // Always SAFE, from any believed state, including LOST and UNKNOWN. The
      // one control that never asks what it thinks is going on first.
      sendCommand(COMMAND_SAFE);
      break;

    case ButtonTarget:
      handleTargetButton();
      break;

    default:
      break;
    }
  }
}

} // namespace

void commandTaskStart() {
  if (xTaskCreate(commandTask, "command", CommandTaskStack, nullptr, CommandTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

uint16_t commandTarget() { return settings.targetId; }

node_state_t commandBelievedState() { return believed; }

belief_t commandBelief() { return belief; }
