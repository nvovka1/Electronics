// Host tests for everything in lib/: the transition table, the auto-arm, the
// frame codec and the replay guard. No board, about a second, runs in CI.
//
//   pio test -e native

#include <string.h>
#include <unity.h>

#include "frame.h"
#include "node_state.h"
#include "replay_guard.h"

// ---------------------------------------------------------------- the table
// Asserted cell by cell against the design spec section 1.1. Repetitive on
// purpose: the point is that it can be read against the spec without anyone
// having to work anything out.

static void expect(node_state_t from, command_t command, transition_kind_t kind,
                   node_state_t to) {
  const transition_t result = state_apply(from, command);
  TEST_ASSERT_EQUAL_INT(kind, result.kind);
  TEST_ASSERT_EQUAL_INT(to, result.state);
}

static void test_table_from_safe(void) {
  expect(STATE_SAFE, COMMAND_INIT, KIND_MOVE, STATE_INIT);
  expect(STATE_SAFE, COMMAND_ARM, KIND_REJECT, STATE_SAFE);
  expect(STATE_SAFE, COMMAND_FIRE, KIND_REJECT, STATE_SAFE);
  expect(STATE_SAFE, COMMAND_SAFE, KIND_NOOP, STATE_SAFE);
}

static void test_table_from_init(void) {
  expect(STATE_INIT, COMMAND_INIT, KIND_RESTART, STATE_INIT);
  expect(STATE_INIT, COMMAND_ARM, KIND_MOVE, STATE_ARMED);
  expect(STATE_INIT, COMMAND_FIRE, KIND_REJECT, STATE_INIT);
  expect(STATE_INIT, COMMAND_SAFE, KIND_MOVE, STATE_SAFE);
}

static void test_table_from_armed(void) {
  expect(STATE_ARMED, COMMAND_INIT, KIND_REJECT, STATE_ARMED);
  expect(STATE_ARMED, COMMAND_ARM, KIND_NOOP, STATE_ARMED);
  expect(STATE_ARMED, COMMAND_FIRE, KIND_MOVE, STATE_FIRE);
  expect(STATE_ARMED, COMMAND_SAFE, KIND_MOVE, STATE_SAFE);
}

static void test_table_from_fire(void) {
  expect(STATE_FIRE, COMMAND_INIT, KIND_REJECT, STATE_FIRE);
  expect(STATE_FIRE, COMMAND_ARM, KIND_REJECT, STATE_FIRE);
  expect(STATE_FIRE, COMMAND_FIRE, KIND_NOOP, STATE_FIRE);
  expect(STATE_FIRE, COMMAND_SAFE, KIND_MOVE, STATE_SAFE);
}

static void test_boot_state_is_safe(void) {
  // A node that loses power mid-sequence must not come back armed or fired.
  TEST_ASSERT_EQUAL_INT(STATE_SAFE, STATE_BOOT);
}

static void test_safe_is_accepted_from_every_state(void) {
  // SAFE is the only revoke path. There must be no state it can be refused
  // from, including FIRE, which is latched.
  for (int state = 0; state < STATE_COUNT; state++) {
    const transition_t result = state_apply((node_state_t)state, COMMAND_SAFE);
    TEST_ASSERT_NOT_EQUAL_INT(KIND_REJECT, result.kind);
    TEST_ASSERT_EQUAL_INT(STATE_SAFE, result.state);
  }
}

static void test_a_rejection_says_why(void) {
  const transition_t result = state_apply(STATE_SAFE, COMMAND_FIRE);
  TEST_ASSERT_EQUAL_INT(KIND_REJECT, result.kind);
  TEST_ASSERT_EQUAL_INT(REASON_BAD_TRANSITION, result.reason);
}

static void test_values_outside_the_enums_are_refused(void) {
  // Both arrive off the radio as bytes chosen by whoever sent the frame.
  transition_t result = state_apply(STATE_ARMED, (command_t)99);
  TEST_ASSERT_EQUAL_INT(KIND_REJECT, result.kind);
  TEST_ASSERT_EQUAL_INT(REASON_BAD_COMMAND, result.reason);

  result = state_apply((node_state_t)200, COMMAND_SAFE);
  TEST_ASSERT_EQUAL_INT(KIND_REJECT, result.kind);
  TEST_ASSERT_EQUAL_INT(REASON_BAD_COMMAND, result.reason);

  TEST_ASSERT_FALSE(command_is_valid(0));
  TEST_ASSERT_FALSE(state_is_valid(4));
}

// ------------------------------------------------------------- the auto-arm

static void test_the_countdown_arms_a_node_in_init(void) {
  const transition_t result = state_auto_arm(STATE_INIT);
  TEST_ASSERT_EQUAL_INT(KIND_MOVE, result.kind);
  TEST_ASSERT_EQUAL_INT(STATE_ARMED, result.state);
}

static void test_the_countdown_does_nothing_from_any_other_state(void) {
  // If this ever succeeds from SAFE, a node sitting idle arms itself with
  // nobody having touched it.
  const node_state_t others[] = {STATE_SAFE, STATE_ARMED, STATE_FIRE};

  for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
    const transition_t result = state_auto_arm(others[i]);
    TEST_ASSERT_EQUAL_INT(KIND_REJECT, result.kind);
    TEST_ASSERT_EQUAL_INT(others[i], result.state);
  }
}

static void test_only_init_starts_a_countdown(void) {
  TEST_ASSERT_TRUE(state_starts_countdown(STATE_INIT));
  TEST_ASSERT_FALSE(state_starts_countdown(STATE_SAFE));
  TEST_ASSERT_FALSE(state_starts_countdown(STATE_ARMED));
  TEST_ASSERT_FALSE(state_starts_countdown(STATE_FIRE));
}

// ------------------------------------------------------------- frame codec

// Plain field assignment rather than designated initializers: those are a GCC
// extension in C++17 and MSVC refuses them, and this file has to build under
// both - `pio test -e native` uses gcc, scripts/run_native_tests.ps1 uses cl.
static void test_a_command_frame_round_trips(void) {
  payload_cmd_t sent;
  sent.dst = 2;
  sent.command = COMMAND_ARM;
  sent.counter = 0xDEADBEEF;

  frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.ver = FRAME_VERSION;
  frame.type = MSG_CMD;
  frame.src = 1;
  frame.seq = 42;
  frame.len = (uint8_t)payload_cmd_encode(&sent, frame.payload, sizeof(frame.payload));
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_SIZE, frame.len);

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));
  TEST_ASSERT_EQUAL_INT(FRAME_OVERHEAD + PAYLOAD_CMD_SIZE, written);

  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(wire, (size_t)written, &decoded));
  TEST_ASSERT_EQUAL_UINT(MSG_CMD, decoded.type);
  TEST_ASSERT_EQUAL_UINT(1, decoded.src);
  TEST_ASSERT_EQUAL_UINT(42, decoded.seq);

  payload_cmd_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_SIZE,
                         payload_cmd_decode(decoded.payload, decoded.len, &back));
  TEST_ASSERT_EQUAL_UINT(sent.dst, back.dst);
  TEST_ASSERT_EQUAL_UINT(sent.command, back.command);
  TEST_ASSERT_EQUAL_UINT32(sent.counter, back.counter);
}

static void test_an_ack_round_trips(void) {
  payload_cmd_ack_t sent;
  sent.counter = 7;
  sent.accepted = 0;
  sent.state = STATE_ARMED;
  sent.reason = REASON_BAD_TRANSITION;

  uint8_t payload[PAYLOAD_CMD_ACK_SIZE];
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_ACK_SIZE,
                         payload_cmd_ack_encode(&sent, payload, sizeof(payload)));

  payload_cmd_ack_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_ACK_SIZE,
                         payload_cmd_ack_decode(payload, sizeof(payload), &back));

  // The ACK carries the resulting state even when the command was refused -
  // that is how a controller that has lost track resynchronises.
  TEST_ASSERT_EQUAL_UINT(STATE_ARMED, back.state);
  TEST_ASSERT_EQUAL_UINT(REASON_BAD_TRANSITION, back.reason);
  TEST_ASSERT_EQUAL_UINT(0, back.accepted);
}

static void test_a_state_announcement_round_trips(void) {
  payload_state_t sent;
  sent.dst = 1;
  sent.state = STATE_ARMED;
  sent.cause = STATE_CAUSE_AUTO_ARM;

  uint8_t payload[PAYLOAD_STATE_SIZE];
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_STATE_SIZE,
                         payload_state_encode(&sent, payload, sizeof(payload)));

  payload_state_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_STATE_SIZE,
                         payload_state_decode(payload, sizeof(payload), &back));
  TEST_ASSERT_EQUAL_UINT(STATE_CAUSE_AUTO_ARM, back.cause);
  TEST_ASSERT_EQUAL_UINT(STATE_ARMED, back.state);
}

static void test_a_corrupted_frame_is_refused(void) {
  payload_cmd_t sent;
  sent.dst = 2;
  sent.command = COMMAND_FIRE;
  sent.counter = 9;

  frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.ver = FRAME_VERSION;
  frame.type = MSG_CMD;
  frame.src = 1;
  frame.len = (uint8_t)payload_cmd_encode(&sent, frame.payload, sizeof(frame.payload));

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));

  // One flipped bit anywhere in the body must fail the CRC. A FIRE frame that
  // arrives damaged must never be acted on.
  wire[FRAME_HEADER_SIZE + 2] ^= 0x01u;

  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, frame_decode(wire, (size_t)written, &decoded));
}

static void test_a_frame_that_lies_about_its_length_is_refused(void) {
  // Trusting LEN over the buffer is how a short packet becomes a read past its
  // end.
  uint8_t wire[FRAME_OVERHEAD + PAYLOAD_CMD_SIZE];
  memset(wire, 0, sizeof(wire));
  wire[0] = FRAME_SYNC;
  wire[1] = FRAME_VERSION;
  wire[2] = MSG_CMD;
  wire[3] = FRAME_MAX_PAYLOAD; // claims more than arrived

  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_LENGTH, frame_decode(wire, sizeof(wire), &decoded));
}

static void test_a_short_or_foreign_frame_is_refused(void) {
  uint8_t tiny[4] = {FRAME_SYNC, FRAME_VERSION, MSG_CMD, 0};
  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_SHORT, frame_decode(tiny, sizeof(tiny), &decoded));

  uint8_t wire[FRAME_OVERHEAD];
  memset(wire, 0, sizeof(wire));
  wire[0] = 0x00; // not our sync byte
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_SYNC, frame_decode(wire, sizeof(wire), &decoded));

  wire[0] = FRAME_SYNC;
  wire[1] = 99; // a version this build does not speak
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_VERSION, frame_decode(wire, sizeof(wire), &decoded));
}

static void test_a_payload_decoder_refuses_the_wrong_size(void) {
  uint8_t buffer[PAYLOAD_CMD_SIZE] = {0};
  payload_cmd_t out;

  TEST_ASSERT_EQUAL_UINT(0, payload_cmd_decode(buffer, PAYLOAD_CMD_SIZE - 1, &out));
  TEST_ASSERT_EQUAL_UINT(0, payload_cmd_decode(buffer, PAYLOAD_CMD_SIZE + 1, &out));
}

// ------------------------------------------------------------ replay guard

static void test_a_new_counter_is_accepted(void) {
  replay_guard_t guard;
  replay_reset(&guard);

  TEST_ASSERT_TRUE(replay_accept(&guard, 1, 1));
  TEST_ASSERT_TRUE(replay_accept(&guard, 1, 2));
  TEST_ASSERT_EQUAL_UINT32(2, replay_last_counter(&guard, 1));
}

static void test_a_repeated_counter_is_refused(void) {
  // The recorded-and-resent FIRE frame. Everything else about it is valid.
  replay_guard_t guard;
  replay_reset(&guard);

  TEST_ASSERT_TRUE(replay_accept(&guard, 1, 5));
  TEST_ASSERT_FALSE(replay_accept(&guard, 1, 5));
  TEST_ASSERT_FALSE(replay_accept(&guard, 1, 4));
}

static void test_counters_are_tracked_per_source(void) {
  // Two controllers count independently; one must not lock the other out.
  replay_guard_t guard;
  replay_reset(&guard);

  TEST_ASSERT_TRUE(replay_accept(&guard, 1, 100));
  TEST_ASSERT_TRUE(replay_accept(&guard, 2, 1));
  TEST_ASSERT_EQUAL_UINT32(100, replay_last_counter(&guard, 1));
  TEST_ASSERT_EQUAL_UINT32(1, replay_last_counter(&guard, 2));
}

static void test_a_zero_counter_is_refused(void) {
  // What an uninitialised counter looks like, and what a controller that lost
  // its NVS would send.
  replay_guard_t guard;
  replay_reset(&guard);

  TEST_ASSERT_FALSE(replay_accept(&guard, 1, 0));
}

static void test_an_unknown_source_starts_at_zero(void) {
  replay_guard_t guard;
  replay_reset(&guard);

  TEST_ASSERT_EQUAL_UINT32(0, replay_last_counter(&guard, 77));
}

static void test_the_table_survives_more_sources_than_slots(void) {
  replay_guard_t guard;
  replay_reset(&guard);

  for (uint16_t src = 1; src <= REPLAY_SLOTS + 2; src++) {
    TEST_ASSERT_TRUE(replay_accept(&guard, src, 10));
  }

  // The most recent source must still be protected, whatever was evicted.
  TEST_ASSERT_FALSE(replay_accept(&guard, REPLAY_SLOTS + 2, 10));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_table_from_safe);
  RUN_TEST(test_table_from_init);
  RUN_TEST(test_table_from_armed);
  RUN_TEST(test_table_from_fire);
  RUN_TEST(test_boot_state_is_safe);
  RUN_TEST(test_safe_is_accepted_from_every_state);
  RUN_TEST(test_a_rejection_says_why);
  RUN_TEST(test_values_outside_the_enums_are_refused);

  RUN_TEST(test_the_countdown_arms_a_node_in_init);
  RUN_TEST(test_the_countdown_does_nothing_from_any_other_state);
  RUN_TEST(test_only_init_starts_a_countdown);

  RUN_TEST(test_a_command_frame_round_trips);
  RUN_TEST(test_an_ack_round_trips);
  RUN_TEST(test_a_state_announcement_round_trips);
  RUN_TEST(test_a_corrupted_frame_is_refused);
  RUN_TEST(test_a_frame_that_lies_about_its_length_is_refused);
  RUN_TEST(test_a_short_or_foreign_frame_is_refused);
  RUN_TEST(test_a_payload_decoder_refuses_the_wrong_size);

  RUN_TEST(test_a_new_counter_is_accepted);
  RUN_TEST(test_a_repeated_counter_is_refused);
  RUN_TEST(test_counters_are_tracked_per_source);
  RUN_TEST(test_a_zero_counter_is_refused);
  RUN_TEST(test_an_unknown_source_starts_at_zero);
  RUN_TEST(test_the_table_survives_more_sources_than_slots);

  return UNITY_END();
}
