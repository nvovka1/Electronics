#pragma once

#include <stdbool.h>
#include <stdint.h>

// The Init/Arm/Fire/Safe rules, and nothing else. No Arduino, no FreeRTOS, no
// clock - so this file builds for the host and every rule below is covered by
// `pio test -e native`.
//
// The backend holds the same table in C#. They are checked against each other
// by a test that parses the block between the TRANSITION-TABLE markers in
// node_state.cpp, because a table that drifts misbehaves in the field rather
// than failing a build. Keep that block's shape if you edit it.
//
// Full description: docs/superpowers/specs/2026-09-17-initiator-system-design.md

#ifdef __cplusplus
extern "C" {
#endif

// The numbers are protocol: they travel in MSG_CMD_ACK and MSG_STATE, and in
// every report to the fleet service. They can never be reordered for
// convenience.
typedef enum {
  STATE_SAFE = 0,
  STATE_INIT = 1,
  STATE_ARMED = 2,
  STATE_FIRE = 3,
  STATE_COUNT = 4,
} node_state_t;

// Deliberately starts at 1. Zero is what an uninitialised byte and a truncated
// payload both look like, and neither should decode to a valid command.
typedef enum {
  COMMAND_INIT = 1,
  COMMAND_ARM = 2,
  COMMAND_FIRE = 3,
  COMMAND_SAFE = 4,
  COMMAND_COUNT = 4,
} command_t;

typedef enum {
  REASON_OK = 0,
  REASON_BAD_TRANSITION = 1,
  REASON_BAD_TARGET = 2,
  REASON_REPLAY = 3,
  REASON_BAD_COMMAND = 4,
} reject_reason_t;

typedef enum {
  SOURCE_LORA = 0,
  SOURCE_API = 1,
  SOURCE_TIMER = 2,
} command_source_t;

// Three of these four are accepted outcomes, which is why "accepted" alone
// cannot describe a transition.
typedef enum {
  KIND_MOVE = 0,    // the state changed
  KIND_NOOP = 1,    // the command for the state we were already in
  KIND_RESTART = 2, // INIT while in INIT: accepted, and the countdown restarts
  KIND_REJECT = 3,  // refused; the state did not change
} transition_kind_t;

typedef struct {
  transition_kind_t kind;
  node_state_t state;     // the state afterwards
  reject_reason_t reason; // REASON_OK unless refused
} transition_t;

// The state every node boots into. Never restored from NVS: a node that loses
// power mid-sequence must not come back armed or fired.
#define STATE_BOOT STATE_SAFE

// Offer a command to a node in `current`.
transition_t state_apply(node_state_t current, command_t command);

// What a node does to itself when the INIT countdown expires. Legal from INIT
// and nowhere else - if this ever succeeds from SAFE, a node sitting idle can
// arm itself with nobody having touched it.
transition_t state_auto_arm(node_state_t current);

// Whether entering this state starts the auto-arm countdown. INIT is the only
// state with a timed exit.
bool state_starts_countdown(node_state_t state);

bool state_is_valid(uint8_t value);
bool command_is_valid(uint8_t value);

// Short upper-case names for the screen and the log. Never null.
const char *state_name(node_state_t state);
const char *command_name(command_t command);
const char *reason_name(reject_reason_t reason);

#ifdef __cplusplus
}
#endif
