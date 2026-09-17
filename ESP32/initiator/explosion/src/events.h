#pragma once

#include <stdint.h>

#include "node_state.h"

// The messages the tasks send each other. Plain C structs, copied by value into
// FreeRTOS queues - nothing here is a pointer, so there is no lifetime to get
// wrong and no allocation after setup.
//
// Tasks share nothing but these queues. That is why there is not a mutex in the
// firmware: there is no shared mutable state to protect.

// A command id from the backend. Long enough for a Mongo ObjectId as hex, with
// room to spare.
#define COMMAND_ID_MAX 28

// A command for the state task, from wherever it came.
struct CommandRequest {
  uint8_t command;        // command_t
  uint8_t source;         // command_source_t
  uint16_t controllerId;  // who sent it over the radio; 0 for the API
  uint32_t counter;       // radio only, for the ACK
  char commandId[COMMAND_ID_MAX]; // API only; empty otherwise
};

// What the LEDs should show. Exactly one LED is lit, and it is always the
// current state.
struct LedRequest {
  uint8_t state; // node_state_t
};

// What the screen should show.
struct UiUpdate {
  uint8_t state;
  uint8_t lastCommand; // command_t, or 0 when nothing has arrived yet
  uint8_t lastSource;  // command_source_t
  bool lastAccepted;
  uint8_t lastReason;
  // Milliseconds left on the auto-arm countdown, or 0 when none is running.
  uint32_t countdownMs;
};

// A transition, on its way to the backend. Buffered when the network is down -
// offline is not an error for this node.
struct StateEventRecord {
  uint32_t timestampMs;
  uint32_t bootCount;
  uint8_t fromState;
  uint8_t toState;
  uint8_t command;   // command_t; ignored when hasCommand is false
  bool hasCommand;   // false for the auto-arm: nothing commanded it
  uint8_t source;    // command_source_t
  bool accepted;
  uint8_t reason;    // reject_reason_t
  char commandId[COMMAND_ID_MAX];
};

// Something for the radio task to transmit.
enum RadioTxKind : uint8_t {
  TxAck = 0,
  TxAnnounce = 1,
};

struct RadioTx {
  uint8_t kind;
  uint16_t dst;
  uint32_t counter; // the command being acknowledged
  uint8_t accepted;
  uint8_t state;
  uint8_t reason;
  uint8_t cause; // announcements only
};
