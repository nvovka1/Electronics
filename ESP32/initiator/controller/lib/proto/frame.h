#pragma once

#include <stddef.h>
#include <stdint.h>

// Initiator frame, protocol version 1.
//
//   off size field    endian  notes
//   0   1    SYNC     -       0xA5
//   1   1    VER      -       protocol version
//   2   1    TYPE     -       message type
//   3   1    LEN      -       payload length, 0..PAYLOAD_MAX
//   4   2    SRC      LE      node id of the sender
//   6   2    SEQ      LE      per-source, wraps at 0xFFFF
//   8   LEN  PAYLOAD  LE      per TYPE
//   8+L 2    CRC16    LE      CCITT-FALSE over bytes [0 .. 8+LEN-1]
//
// Deliberately the same envelope as ESP32/lora-queue-release: same sync byte,
// same header, same CRC. That project is not modified and this is a separate
// copy, but keeping the envelope identical means the two families of node can
// share air without decoding each other's traffic as garbage - they simply see
// a TYPE they do not handle and drop it.

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_SYNC 0xA5u
#define FRAME_VERSION 1u

enum {
  FRAME_HEADER_SIZE = 8,
  FRAME_CRC_SIZE = 2,
  FRAME_OVERHEAD = FRAME_HEADER_SIZE + FRAME_CRC_SIZE,
  FRAME_MAX_PAYLOAD = 16,
  FRAME_MAX_SIZE = FRAME_OVERHEAD + FRAME_MAX_PAYLOAD,
};

// Type codes are handed out with room to grow:
//   0x01..0x3F  core types
//   0x40..0x7F  vendor / experimental
//   0x80..0xFF  reserved
enum {
  MSG_CMD = 0x10,     // controller -> node: do this
  MSG_CMD_ACK = 0x11, // node -> controller: here is what I did, and where I am
  MSG_STATE = 0x12,   // node -> controller: I moved on my own
};

// Everyone. Used for an all-SAFE; a node never answers a broadcast, because an
// ACK would tell the sender its command reached one particular node.
#define NODE_ID_BROADCAST 0xFFFFu

typedef struct {
  uint8_t ver;
  uint8_t type;
  uint16_t src;
  uint16_t seq;
  uint8_t len;
  uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_t;

typedef enum {
  FRAME_OK = 0,
  FRAME_ERR_ARG,     // null pointer, or a buffer too small for the result
  FRAME_ERR_SHORT,   // fewer bytes than a header plus a CRC
  FRAME_ERR_SYNC,    // first byte is not 0xA5
  FRAME_ERR_VERSION, // a VER this build does not speak
  FRAME_ERR_LENGTH,  // LEN over the MTU, or disagreeing with the buffer
  FRAME_ERR_CRC,     // intact body, but the CRC does not match
} frame_result_t;

// Returns bytes written, or -(frame_result_t) on failure.
int frame_encode(const frame_t *frame, uint8_t *out, size_t capacity);

// Strict: every field is checked before the payload is copied out. A frame that
// fails here never reaches the state machine.
frame_result_t frame_decode(const uint8_t *in, size_t len, frame_t *out);

const char *frame_strerror(frame_result_t result);

// --- payloads -------------------------------------------------------------
// Sizes are fixed per type and are part of the protocol. Each codec returns the
// number of bytes it wrote or read, or 0 when the buffer is the wrong size.

typedef struct {
  uint16_t dst;
  uint8_t command;
  // Monotonic per source, kept in NVS across reboots. A node refuses a counter
  // it has already seen: without it, a recorded FIRE frame can be replayed.
  uint32_t counter;
} payload_cmd_t;
#define PAYLOAD_CMD_SIZE 7

typedef struct {
  uint32_t counter; // the command being answered
  uint8_t accepted;
  uint8_t state;  // the node's state afterwards, accepted or not
  uint8_t reason;
} payload_cmd_ack_t;
#define PAYLOAD_CMD_ACK_SIZE 7

typedef struct {
  uint16_t dst;
  uint8_t state;
  uint8_t cause; // 1 = auto_arm
} payload_state_t;
#define PAYLOAD_STATE_SIZE 4

#define STATE_CAUSE_AUTO_ARM 1u

size_t payload_cmd_encode(const payload_cmd_t *in, uint8_t *out, size_t capacity);
size_t payload_cmd_decode(const uint8_t *in, size_t len, payload_cmd_t *out);

size_t payload_cmd_ack_encode(const payload_cmd_ack_t *in, uint8_t *out, size_t capacity);
size_t payload_cmd_ack_decode(const uint8_t *in, size_t len, payload_cmd_ack_t *out);

size_t payload_state_encode(const payload_state_t *in, uint8_t *out, size_t capacity);
size_t payload_state_decode(const uint8_t *in, size_t len, payload_state_t *out);

#ifdef __cplusplus
}
#endif
