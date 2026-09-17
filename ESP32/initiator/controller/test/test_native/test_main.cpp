// Host tests for everything in lib/: the sequence and the frame codec.
//
//   pio test -e native
//   powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1

#include <string.h>
#include <unity.h>

#include "frame.h"
#include "sequence.h"

// ----------------------------------------------------------- the sequence

static void test_the_sequence_advances(void) {
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_SAFE, BELIEF_KNOWN));
  TEST_ASSERT_EQUAL_INT(COMMAND_ARM, sequence_next(STATE_INIT, BELIEF_KNOWN));
  TEST_ASSERT_EQUAL_INT(COMMAND_FIRE, sequence_next(STATE_ARMED, BELIEF_KNOWN));
}

static void test_nothing_follows_fire(void) {
  // FIRE is latched and only SAFE leaves it, which has a button of its own. The
  // sequence button must not send something the node is certain to refuse.
  TEST_ASSERT_EQUAL_INT(COMMAND_NONE, sequence_next(STATE_FIRE, BELIEF_KNOWN));
}

static void test_an_unknown_node_starts_at_init(void) {
  // Nothing has been heard from this node. INIT is the start of the sequence
  // and its ACK reports where the node actually is.
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_SAFE, BELIEF_UNKNOWN));
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_ARMED, BELIEF_UNKNOWN));
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_FIRE, BELIEF_UNKNOWN));
}

static void test_a_stale_belief_starts_at_init(void) {
  // The retries ran out, so the last belief may be wrong - the command might
  // have arrived and only the ACK been lost. Carrying on from a state we are
  // not sure of is how a FIRE gets sent to a node that is still in SAFE.
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_ARMED, BELIEF_LOST));
  TEST_ASSERT_EQUAL_INT(COMMAND_INIT, sequence_next(STATE_INIT, BELIEF_LOST));
}

static void test_the_sequence_never_proposes_safe(void) {
  // SAFE has its own button and is never something the sequence advances into.
  for (int state = 0; state < STATE_COUNT; state++) {
    for (int belief = 0; belief <= BELIEF_LOST; belief++) {
      TEST_ASSERT_NOT_EQUAL_INT(
          COMMAND_SAFE, sequence_next((node_state_t)state, (belief_t)belief));
    }
  }
}

static void test_the_sequence_never_proposes_an_invalid_command(void) {
  for (int state = 0; state < STATE_COUNT; state++) {
    const command_t next = sequence_next((node_state_t)state, BELIEF_KNOWN);
    if (next == COMMAND_NONE) continue;

    TEST_ASSERT_TRUE(command_is_valid((uint8_t)next));
  }
}

static void test_values_outside_the_enums_are_handled(void) {
  // The believed state arrives from the node as a byte.
  TEST_ASSERT_EQUAL_INT(COMMAND_NONE, sequence_next((node_state_t)200, BELIEF_KNOWN));
  TEST_ASSERT_FALSE(state_is_valid(200));
  TEST_ASSERT_FALSE(command_is_valid(0));
}

// ------------------------------------------------------------ frame codec
// The controller's own copy of the codec. These tests exist separately from the
// node's because the two copies are allowed to diverge - and if they ever do,
// this is where it shows up first.

static void test_a_command_frame_round_trips(void) {
  payload_cmd_t sent;
  sent.dst = 2;
  sent.command = COMMAND_FIRE;
  sent.counter = 123456;

  frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.ver = FRAME_VERSION;
  frame.type = MSG_CMD;
  frame.src = 100;
  frame.seq = 7;
  frame.len = (uint8_t)payload_cmd_encode(&sent, frame.payload, sizeof(frame.payload));

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));
  TEST_ASSERT_EQUAL_INT(FRAME_OVERHEAD + PAYLOAD_CMD_SIZE, written);

  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(wire, (size_t)written, &decoded));
  TEST_ASSERT_EQUAL_UINT(100, decoded.src);

  payload_cmd_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_SIZE,
                         payload_cmd_decode(decoded.payload, decoded.len, &back));
  TEST_ASSERT_EQUAL_UINT(2, back.dst);
  TEST_ASSERT_EQUAL_UINT(COMMAND_FIRE, back.command);
  TEST_ASSERT_EQUAL_UINT32(123456, back.counter);
}

static void test_an_ack_decodes(void) {
  payload_cmd_ack_t sent;
  sent.counter = 42;
  sent.accepted = 0;
  sent.state = STATE_SAFE;
  sent.reason = REASON_BAD_TRANSITION;

  uint8_t payload[PAYLOAD_CMD_ACK_SIZE];
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_ACK_SIZE,
                         payload_cmd_ack_encode(&sent, payload, sizeof(payload)));

  payload_cmd_ack_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_CMD_ACK_SIZE,
                         payload_cmd_ack_decode(payload, sizeof(payload), &back));

  // A refusal still carries the node's state. That is what lets the controller
  // correct itself instead of only reporting a failure.
  TEST_ASSERT_EQUAL_UINT(STATE_SAFE, back.state);
  TEST_ASSERT_EQUAL_UINT(REASON_BAD_TRANSITION, back.reason);
}

static void test_an_announcement_decodes(void) {
  payload_state_t sent;
  sent.dst = 100;
  sent.state = STATE_ARMED;
  sent.cause = STATE_CAUSE_AUTO_ARM;

  uint8_t payload[PAYLOAD_STATE_SIZE];
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_STATE_SIZE,
                         payload_state_encode(&sent, payload, sizeof(payload)));

  payload_state_t back;
  TEST_ASSERT_EQUAL_UINT(PAYLOAD_STATE_SIZE,
                         payload_state_decode(payload, sizeof(payload), &back));
  TEST_ASSERT_EQUAL_UINT(100, back.dst);
  TEST_ASSERT_EQUAL_UINT(STATE_ARMED, back.state);
  TEST_ASSERT_EQUAL_UINT(STATE_CAUSE_AUTO_ARM, back.cause);
}

static void test_a_corrupted_ack_is_refused(void) {
  payload_cmd_ack_t sent;
  sent.counter = 1;
  sent.accepted = 1;
  sent.state = STATE_ARMED;
  sent.reason = REASON_OK;

  frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.ver = FRAME_VERSION;
  frame.type = MSG_CMD_ACK;
  frame.src = 2;
  frame.len = (uint8_t)payload_cmd_ack_encode(&sent, frame.payload, sizeof(frame.payload));

  uint8_t wire[FRAME_MAX_SIZE];
  const int written = frame_encode(&frame, wire, sizeof(wire));

  // A damaged ACK must not be believed: it would move the controller's belief
  // to whatever the corruption happened to say.
  wire[FRAME_HEADER_SIZE + 5] ^= 0x08u;

  frame_t decoded;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, frame_decode(wire, (size_t)written, &decoded));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_the_sequence_advances);
  RUN_TEST(test_nothing_follows_fire);
  RUN_TEST(test_an_unknown_node_starts_at_init);
  RUN_TEST(test_a_stale_belief_starts_at_init);
  RUN_TEST(test_the_sequence_never_proposes_safe);
  RUN_TEST(test_the_sequence_never_proposes_an_invalid_command);
  RUN_TEST(test_values_outside_the_enums_are_handled);

  RUN_TEST(test_a_command_frame_round_trips);
  RUN_TEST(test_an_ack_decodes);
  RUN_TEST(test_an_announcement_decodes);
  RUN_TEST(test_a_corrupted_ack_is_refused);

  return UNITY_END();
}
