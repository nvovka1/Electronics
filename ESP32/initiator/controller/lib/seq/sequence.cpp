#include "sequence.h"

command_t sequence_next(node_state_t believed, belief_t belief) {
  // Nothing heard from this node, or the last exchange was lost and the belief
  // is stale. Either way the sequence starts at the beginning: INIT is refused
  // from nowhere except FIRE, and its ACK will say where the node actually is.
  if (belief != BELIEF_KNOWN) return COMMAND_INIT;

  switch (believed) {
  case STATE_SAFE:  return COMMAND_INIT;
  case STATE_INIT:  return COMMAND_ARM;
  case STATE_ARMED: return COMMAND_FIRE;

  // Nothing follows FIRE. It is latched, and the only way out is SAFE, which
  // has a button of its own - so the sequence button does nothing here rather
  // than sending something the node would refuse.
  case STATE_FIRE:
  default:
    return COMMAND_NONE;
  }
}

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
  default:           return "-";
  }
}

const char *reason_name(reject_reason_t reason) {
  switch (reason) {
  case REASON_OK:             return "ok";
  case REASON_BAD_TRANSITION: return "not from there";
  case REASON_BAD_TARGET:     return "wrong node";
  case REASON_REPLAY:         return "already seen";
  case REASON_BAD_COMMAND:    return "bad command";
  default:                    return "?";
  }
}

bool state_is_valid(uint8_t value) { return value < (uint8_t)STATE_COUNT; }

bool command_is_valid(uint8_t value) {
  return value >= (uint8_t)COMMAND_INIT && value <= (uint8_t)COMMAND_SAFE;
}
