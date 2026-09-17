#include "node_state.h"

// The table from the design spec section 1.1, in the same shape: a row per
// state, a column per command, columns in the order INIT, ARM, FIRE, SAFE.
//
// A literal table rather than a nest of ifs, so it can be read against the spec
// cell by cell - which is the only way anyone will ever verify it.
//
// The backend's contract test parses everything between the two markers below
// and asserts its own C# table agrees, cell by cell. If you change the shape of
// these lines, change that parser with them.

// TRANSITION-TABLE-BEGIN
static const transition_t TRANSITION_TABLE[STATE_COUNT][COMMAND_COUNT] = {
    //             INIT                        ARM                         FIRE                        SAFE
    /* SAFE  */ { {KIND_MOVE, STATE_INIT},   {KIND_REJECT, STATE_SAFE},  {KIND_REJECT, STATE_SAFE},  {KIND_NOOP, STATE_SAFE}  },
    /* INIT  */ { {KIND_RESTART, STATE_INIT},{KIND_MOVE, STATE_ARMED},   {KIND_REJECT, STATE_INIT},  {KIND_MOVE, STATE_SAFE}  },
    /* ARMED */ { {KIND_REJECT, STATE_ARMED},{KIND_NOOP, STATE_ARMED},   {KIND_MOVE, STATE_FIRE},    {KIND_MOVE, STATE_SAFE}  },
    /* FIRE  */ { {KIND_REJECT, STATE_FIRE}, {KIND_REJECT, STATE_FIRE},  {KIND_NOOP, STATE_FIRE},    {KIND_MOVE, STATE_SAFE}  },
};
// TRANSITION-TABLE-END

// The commands are 1..4 and the columns 0..3. Written once, here, rather than
// as a `- 1` scattered through the file.
static int command_column(command_t command) { return (int)command - 1; }

bool state_is_valid(uint8_t value) { return value < (uint8_t)STATE_COUNT; }

bool command_is_valid(uint8_t value) {
  return value >= (uint8_t)COMMAND_INIT && value <= (uint8_t)COMMAND_SAFE;
}

transition_t state_apply(node_state_t current, command_t command) {
  // Both arrive off the radio as bytes. Reaching the table with a value outside
  // it would be an out-of-range index chosen by whoever sent the frame.
  if (!state_is_valid((uint8_t)current) || !command_is_valid((uint8_t)command)) {
    transition_t refused = {KIND_REJECT, current, REASON_BAD_COMMAND};
    return refused;
  }

  const transition_t cell = TRANSITION_TABLE[current][command_column(command)];

  transition_t result;
  result.kind = cell.kind;
  result.state = cell.state;
  result.reason = (cell.kind == KIND_REJECT) ? REASON_BAD_TRANSITION : REASON_OK;
  return result;
}

transition_t state_auto_arm(node_state_t current) {
  if (current == STATE_INIT) {
    transition_t armed = {KIND_MOVE, STATE_ARMED, REASON_OK};
    return armed;
  }

  transition_t refused = {KIND_REJECT, current, REASON_BAD_TRANSITION};
  return refused;
}

bool state_starts_countdown(node_state_t state) { return state == STATE_INIT; }

const char *state_name(node_state_t state) {
  switch (state) {
  case STATE_SAFE:  return "SAFE";
  case STATE_INIT:  return "INIT";
  case STATE_ARMED: return "ARMED";
  case STATE_FIRE:  return "FIRE";
  default:          return "?";
  }
}

const char *command_name(command_t command) {
  switch (command) {
  case COMMAND_INIT: return "INIT";
  case COMMAND_ARM:  return "ARM";
  case COMMAND_FIRE: return "FIRE";
  case COMMAND_SAFE: return "SAFE";
  default:           return "?";
  }
}

const char *reason_name(reject_reason_t reason) {
  switch (reason) {
  case REASON_OK:             return "ok";
  case REASON_BAD_TRANSITION: return "not from that state";
  case REASON_BAD_TARGET:     return "another node";
  case REASON_REPLAY:         return "replay";
  case REASON_BAD_COMMAND:    return "bad command";
  default:                    return "?";
  }
}
