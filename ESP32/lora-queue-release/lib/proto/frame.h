#pragma once

#include <stddef.h>
#include <stdint.h>

// LQ-MORSE frame, protocol version 1. The full description lives in
// docs/PROTOCOL.md; this header is the normative field layout it documents.
//
//   off size field    endian  notes
//   0   1    SYNC     -       0xA5
//   1   1    VER      -       protocol version
//   2   1    TYPE     -       message type
//   3   1    LEN      -       payload length, 0..48
//   4   2    SRC      LE      node id of the sender
//   6   2    SEQ      LE      per-source, wraps at 0xFFFF
//   8   LEN  PAYLOAD  LE      per TYPE
//   8+L 2    CRC16    LE      CCITT-FALSE over bytes [0 .. 8+LEN-1]

#ifndef PROTO_VERSION
#define PROTO_VERSION 1
#endif

#define FRAME_SYNC 0xA5u

enum {
  FRAME_HEADER_SIZE = 8,
  FRAME_CRC_SIZE = 2,
  FRAME_OVERHEAD = FRAME_HEADER_SIZE + FRAME_CRC_SIZE, // 10
  FRAME_MAX_PAYLOAD = 48,
  FRAME_MAX_SIZE = FRAME_OVERHEAD + FRAME_MAX_PAYLOAD, // 58
};

// Type codes are handed out with room to grow, not "по оку":
//   0x01..0x3F  core types, defined here
//   0x40..0x7F  vendor / experimental, never assigned by this spec
//   0x80..0xFF  reserved
enum {
  MSG_SYMBOL = 0x01,
  MSG_ACK = 0x02,
  MSG_HEALTH = 0x03,
  MSG_CORE_MAX = 0x3F,
  MSG_VENDOR_MIN = 0x40,
  MSG_VENDOR_MAX = 0x7F,
};

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
  FRAME_ERR_ARG,     // null pointer or a buffer too small to hold the result
  FRAME_ERR_SHORT,   // fewer bytes than a header plus a CRC
  FRAME_ERR_SYNC,    // first byte is not 0xA5
  FRAME_ERR_VERSION, // VER this build does not speak
  FRAME_ERR_LENGTH,  // LEN over the MTU, or disagreeing with the buffer
  FRAME_ERR_CRC,     // body is intact but the CRC does not match
  FRAME_ERR_TYPE,    // well-formed, but the type code is unknown here
} frame_result_t;

// Returns the number of bytes written, or -(frame_result_t) on failure.
int frame_encode(const frame_t *f, uint8_t *out, size_t out_cap);

// Strict: every field is checked before the payload is copied out. A frame
// that fails here never reaches the application.
frame_result_t frame_decode(const uint8_t *in, size_t len, frame_t *out);

const char *frame_strerror(frame_result_t r);
const char *frame_type_name(uint8_t type);

// --- payload codecs -------------------------------------------------------
// Each returns the number of bytes it wrote/read, or 0 when the buffer is too
// small. Sizes are fixed per type and are part of the protocol.

typedef struct {
  uint8_t symbol; // '.' or '-'
  uint32_t key_ms; // sender's monotonic clock when the key was classified
} payload_symbol_t;
#define PAYLOAD_SYMBOL_SIZE 5

typedef struct {
  uint16_t ack_seq;
  int8_t rssi;   // dBm
  int8_t snr_q2; // dB in quarters
} payload_ack_t;
#define PAYLOAD_ACK_SIZE 4

// Twelve bytes once an hour, and out of them the whole picture of the network
// is built. Every field earns its place.
typedef struct {
  uint16_t fw_hash;   // low 16 bits of the git hash: who has not updated yet
  uint32_t uptime_s;  // 3 bytes on the wire: silent reboots are invisible otherwise
  uint8_t reboots;    // climbing means something falls over systematically
  uint8_t last_crash; // a code from log_dict.csv, not text
  uint8_t vbat_dv;    // tenths of a volt, already calibrated
  int16_t last_rssi;  // the link degrades long before it is lost
  uint16_t post_mask; // one bit per block: it is visible what fell off
} payload_health_t;
#define PAYLOAD_HEALTH_SIZE 12

size_t payload_symbol_put(uint8_t *buf, size_t cap, const payload_symbol_t *p);
size_t payload_symbol_get(const uint8_t *buf, size_t len, payload_symbol_t *p);
size_t payload_ack_put(uint8_t *buf, size_t cap, const payload_ack_t *p);
size_t payload_ack_get(const uint8_t *buf, size_t len, payload_ack_t *p);
size_t payload_health_put(uint8_t *buf, size_t cap, const payload_health_t *p);
size_t payload_health_get(const uint8_t *buf, size_t len, payload_health_t *p);
