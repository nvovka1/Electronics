#pragma once

#include <stdint.h>

#include "sequence.h"

// The messages the tasks send each other. Plain C structs copied by value into
// FreeRTOS queues - no pointers, so no lifetime to get wrong and no allocation
// after setup.

// Three buttons, one job each.
enum ButtonId : uint8_t {
  ButtonSequence = 0,
  ButtonSafe = 1,
  ButtonTarget = 2,
};

struct ButtonEvent {
  uint8_t button;
};

// Something the radio task decoded and the command task needs to see: either an
// answer to a command we sent, or a node telling us it moved on its own.
enum RadioRxKind : uint8_t {
  RxAck = 0,
  RxAnnounce = 1,
};

struct RadioRx {
  uint8_t kind;
  uint16_t src;     // the node that sent it
  uint32_t counter; // ACK only: which command is being answered
  uint8_t accepted; // ACK only
  uint8_t state;    // the node's state, in both kinds
  uint8_t reason;   // ACK only
  uint8_t cause;    // announcement only
};

// What the last exchange came to, for the screen.
enum ResultKind : uint8_t {
  ResultNone = 0,
  ResultAccepted = 1,
  ResultRefused = 2,
  ResultLost = 3,     // the retries ran out
  ResultNothingToDo = 4, // the sequence has no next command from here
};

struct UiUpdate {
  uint16_t targetId;
  uint8_t believedState;
  uint8_t belief;       // belief_t
  uint8_t lastCommand;  // command_t
  uint8_t result;       // ResultKind
  uint8_t reason;       // reject_reason_t, when refused
  uint8_t attempts;     // how many sends the last command took
  int16_t rssi;
  bool announced;       // the last change came from the node, not from us
};
