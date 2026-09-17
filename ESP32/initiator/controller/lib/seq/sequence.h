#pragma once

#include <stdbool.h>
#include <stdint.h>

// What the controller knows about the sequence, and nothing else.
//
// THE CONTROLLER DOES NOT HOLD THE TRANSITION TABLE. It has no business
// deciding whether a command is legal: the node holds the state, so the node
// judges. All this needs is which command comes next when you press the button
// once more, which is a three-entry list.
//
// Keeping it to that is what stops the controller from being a second opinion
// that can disagree with the node. It proposes; the node decides; the ACK says
// what actually happened.
//
// Pure: no Arduino, no radio. Built and tested on the host.

#ifdef __cplusplus
extern "C" {
#endif

// The numbers are protocol and must match the node's. They travel in MSG_CMD,
// MSG_CMD_ACK and MSG_STATE.
typedef enum {
  STATE_SAFE = 0,
  STATE_INIT = 1,
  STATE_ARMED = 2,
  STATE_FIRE = 3,
  STATE_COUNT = 4,
} node_state_t;

typedef enum {
  COMMAND_NONE = 0, // "there is nothing after this" - never sent
  COMMAND_INIT = 1,
  COMMAND_ARM = 2,
  COMMAND_FIRE = 3,
  COMMAND_SAFE = 4,
} command_t;

typedef enum {
  REASON_OK = 0,
  REASON_BAD_TRANSITION = 1,
  REASON_BAD_TARGET = 2,
  REASON_REPLAY = 3,
  REASON_BAD_COMMAND = 4,
} reject_reason_t;

// What the controller currently believes about the node it is aimed at.
typedef enum {
  BELIEF_UNKNOWN = 0, // nothing heard from this node yet
  BELIEF_KNOWN = 1,   // an ACK or an announcement said so
  BELIEF_LOST = 2,    // the retries ran out; the last belief is stale
} belief_t;

// The command that comes next for a believed state:
//
//   SAFE  -> INIT
//   INIT  -> ARM
//   ARMED -> FIRE
//   FIRE  -> nothing; only SAFE leaves that state, and SAFE has its own button
//
// A node whose state is not known yet gets INIT: the start of the sequence is
// the only safe place to begin.
command_t sequence_next(node_state_t believed, belief_t belief);

// Short upper-case names for the screen. Never null.
const char *state_name(node_state_t state);
const char *command_name(command_t command);
const char *reason_name(reject_reason_t reason);

bool state_is_valid(uint8_t value);
bool command_is_valid(uint8_t value);

#ifdef __cplusplus
}
#endif
