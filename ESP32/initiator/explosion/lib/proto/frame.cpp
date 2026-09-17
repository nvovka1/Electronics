#include "frame.h"

#include <string.h>

#include "crc16.h"

// Little-endian helpers. Written out rather than memcpy'd from a struct,
// because a struct's layout is the compiler's business and a wire format is
// not.
static void put_u16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)((value >> 8) & 0xFFu);
  out[2] = (uint8_t)((value >> 16) & 0xFFu);
  out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *in) {
  return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

int frame_encode(const frame_t *frame, uint8_t *out, size_t capacity) {
  if (frame == NULL || out == NULL) return -(int)FRAME_ERR_ARG;
  if (frame->len > FRAME_MAX_PAYLOAD) return -(int)FRAME_ERR_LENGTH;

  const size_t total = (size_t)FRAME_OVERHEAD + frame->len;
  if (capacity < total) return -(int)FRAME_ERR_ARG;

  out[0] = FRAME_SYNC;
  out[1] = frame->ver;
  out[2] = frame->type;
  out[3] = frame->len;
  put_u16(&out[4], frame->src);
  put_u16(&out[6], frame->seq);
  memcpy(&out[FRAME_HEADER_SIZE], frame->payload, frame->len);

  const uint16_t crc = crc16_ccitt_false(out, (size_t)FRAME_HEADER_SIZE + frame->len);
  put_u16(&out[FRAME_HEADER_SIZE + frame->len], crc);

  return (int)total;
}

frame_result_t frame_decode(const uint8_t *in, size_t len, frame_t *out) {
  if (in == NULL || out == NULL) return FRAME_ERR_ARG;
  if (len < (size_t)FRAME_OVERHEAD) return FRAME_ERR_SHORT;
  if (in[0] != FRAME_SYNC) return FRAME_ERR_SYNC;
  if (in[1] != FRAME_VERSION) return FRAME_ERR_VERSION;

  const uint8_t payload_len = in[3];
  if (payload_len > FRAME_MAX_PAYLOAD) return FRAME_ERR_LENGTH;

  // The declared length has to agree with what actually arrived. Trusting LEN
  // over the buffer is how a short packet turns into a read past its end.
  if (len != (size_t)FRAME_OVERHEAD + payload_len) return FRAME_ERR_LENGTH;

  const uint16_t expected = crc16_ccitt_false(in, (size_t)FRAME_HEADER_SIZE + payload_len);
  const uint16_t actual = get_u16(&in[FRAME_HEADER_SIZE + payload_len]);
  if (expected != actual) return FRAME_ERR_CRC;

  out->ver = in[1];
  out->type = in[2];
  out->len = payload_len;
  out->src = get_u16(&in[4]);
  out->seq = get_u16(&in[6]);
  memcpy(out->payload, &in[FRAME_HEADER_SIZE], payload_len);

  // An unknown TYPE is not an error here. Deciding what to do with a message
  // this build does not handle belongs to the caller, and a node from a
  // neighbouring project sharing the air is not a corrupt frame.
  return FRAME_OK;
}

const char *frame_strerror(frame_result_t result) {
  switch (result) {
  case FRAME_OK:          return "ok";
  case FRAME_ERR_ARG:     return "bad argument";
  case FRAME_ERR_SHORT:   return "too short";
  case FRAME_ERR_SYNC:    return "bad sync";
  case FRAME_ERR_VERSION: return "wrong version";
  case FRAME_ERR_LENGTH:  return "bad length";
  case FRAME_ERR_CRC:     return "bad crc";
  default:                return "?";
  }
}

// --- payloads -------------------------------------------------------------

size_t payload_cmd_encode(const payload_cmd_t *in, uint8_t *out, size_t capacity) {
  if (in == NULL || out == NULL || capacity < PAYLOAD_CMD_SIZE) return 0;

  put_u16(&out[0], in->dst);
  out[2] = in->command;
  put_u32(&out[3], in->counter);
  return PAYLOAD_CMD_SIZE;
}

size_t payload_cmd_decode(const uint8_t *in, size_t len, payload_cmd_t *out) {
  if (in == NULL || out == NULL || len != PAYLOAD_CMD_SIZE) return 0;

  out->dst = get_u16(&in[0]);
  out->command = in[2];
  out->counter = get_u32(&in[3]);
  return PAYLOAD_CMD_SIZE;
}

size_t payload_cmd_ack_encode(const payload_cmd_ack_t *in, uint8_t *out, size_t capacity) {
  if (in == NULL || out == NULL || capacity < PAYLOAD_CMD_ACK_SIZE) return 0;

  put_u32(&out[0], in->counter);
  out[4] = in->accepted;
  out[5] = in->state;
  out[6] = in->reason;
  return PAYLOAD_CMD_ACK_SIZE;
}

size_t payload_cmd_ack_decode(const uint8_t *in, size_t len, payload_cmd_ack_t *out) {
  if (in == NULL || out == NULL || len != PAYLOAD_CMD_ACK_SIZE) return 0;

  out->counter = get_u32(&in[0]);
  out->accepted = in[4];
  out->state = in[5];
  out->reason = in[6];
  return PAYLOAD_CMD_ACK_SIZE;
}

size_t payload_state_encode(const payload_state_t *in, uint8_t *out, size_t capacity) {
  if (in == NULL || out == NULL || capacity < PAYLOAD_STATE_SIZE) return 0;

  put_u16(&out[0], in->dst);
  out[2] = in->state;
  out[3] = in->cause;
  return PAYLOAD_STATE_SIZE;
}

size_t payload_state_decode(const uint8_t *in, size_t len, payload_state_t *out) {
  if (in == NULL || out == NULL || len != PAYLOAD_STATE_SIZE) return 0;

  out->dst = get_u16(&in[0]);
  out->state = in[2];
  out->cause = in[3];
  return PAYLOAD_STATE_SIZE;
}
