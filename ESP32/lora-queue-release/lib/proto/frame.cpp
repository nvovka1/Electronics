#include "frame.h"

#include <string.h>

#include "crc16.h"

// Little-endian helpers. The endianness of every multi-byte field is stated in
// the spec and enforced here, so the codec is identical on the ESP32 and on
// the host that runs the tests.
static inline void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

static inline uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool type_is_known(uint8_t type) {
  return type == MSG_SYMBOL || type == MSG_ACK || type == MSG_HEALTH;
}

int frame_encode(const frame_t *f, uint8_t *out, size_t out_cap) {
  if (!f || !out) return -FRAME_ERR_ARG;
  if (f->len > FRAME_MAX_PAYLOAD) return -FRAME_ERR_LENGTH;

  const size_t total = (size_t)FRAME_OVERHEAD + f->len;
  if (out_cap < total) return -FRAME_ERR_ARG;

  out[0] = FRAME_SYNC;
  out[1] = f->ver;
  out[2] = f->type;
  out[3] = f->len;
  put_u16(&out[4], f->src);
  put_u16(&out[6], f->seq);
  if (f->len) memcpy(&out[FRAME_HEADER_SIZE], f->payload, f->len);

  // The CRC covers the header as well as the payload: otherwise SRC could be
  // swapped without touching the useful data.
  const uint16_t crc = crc16_ccitt_false(out, FRAME_HEADER_SIZE + f->len);
  put_u16(&out[FRAME_HEADER_SIZE + f->len], crc);

  return (int)total;
}

frame_result_t frame_decode(const uint8_t *in, size_t len, frame_t *out) {
  if (!in || !out) return FRAME_ERR_ARG;
  if (len < (size_t)FRAME_OVERHEAD) return FRAME_ERR_SHORT;
  if (in[0] != FRAME_SYNC) return FRAME_ERR_SYNC;
  if (in[1] != (uint8_t)PROTO_VERSION) return FRAME_ERR_VERSION;

  const uint8_t payload_len = in[3];
  if (payload_len > FRAME_MAX_PAYLOAD) return FRAME_ERR_LENGTH;
  // LEN must agree with what actually arrived, exactly. A frame with trailing
  // bytes is a different frame, not this one with rubbish attached.
  if ((size_t)FRAME_OVERHEAD + payload_len != len) return FRAME_ERR_LENGTH;

  const uint16_t want = crc16_ccitt_false(in, (size_t)FRAME_HEADER_SIZE + payload_len);
  const uint16_t got = get_u16(&in[FRAME_HEADER_SIZE + payload_len]);
  if (want != got) return FRAME_ERR_CRC;

  out->ver = in[1];
  out->type = in[2];
  out->len = payload_len;
  out->src = get_u16(&in[4]);
  out->seq = get_u16(&in[6]);
  memset(out->payload, 0, sizeof(out->payload));
  if (payload_len) memcpy(out->payload, &in[FRAME_HEADER_SIZE], payload_len);

  // Reported last, and only after the struct is filled in: an unknown type is
  // still a valid frame, and the caller may want to log SRC and SEQ from it.
  if (!type_is_known(out->type)) return FRAME_ERR_TYPE;

  return FRAME_OK;
}

const char *frame_strerror(frame_result_t r) {
  switch (r) {
    case FRAME_OK: return "ok";
    case FRAME_ERR_ARG: return "bad argument";
    case FRAME_ERR_SHORT: return "too short";
    case FRAME_ERR_SYNC: return "bad sync";
    case FRAME_ERR_VERSION: return "bad version";
    case FRAME_ERR_LENGTH: return "bad length";
    case FRAME_ERR_CRC: return "crc mismatch";
    case FRAME_ERR_TYPE: return "unknown type";
  }
  return "unknown error";
}

const char *frame_type_name(uint8_t type) {
  switch (type) {
    case MSG_SYMBOL: return "SYMBOL";
    case MSG_ACK: return "ACK";
    case MSG_HEALTH: return "HEALTH";
    default: break;
  }
  if (type >= MSG_VENDOR_MIN && type <= MSG_VENDOR_MAX) return "VENDOR";
  return "RESERVED";
}

// --- payload codecs -------------------------------------------------------

size_t payload_symbol_put(uint8_t *buf, size_t cap, const payload_symbol_t *p) {
  if (!buf || !p || cap < PAYLOAD_SYMBOL_SIZE) return 0;
  buf[0] = p->symbol;
  put_u32(&buf[1], p->key_ms);
  return PAYLOAD_SYMBOL_SIZE;
}

size_t payload_symbol_get(const uint8_t *buf, size_t len, payload_symbol_t *p) {
  if (!buf || !p || len < PAYLOAD_SYMBOL_SIZE) return 0;
  p->symbol = buf[0];
  p->key_ms = get_u32(&buf[1]);
  return PAYLOAD_SYMBOL_SIZE;
}

size_t payload_ack_put(uint8_t *buf, size_t cap, const payload_ack_t *p) {
  if (!buf || !p || cap < PAYLOAD_ACK_SIZE) return 0;
  put_u16(&buf[0], p->ack_seq);
  buf[2] = (uint8_t)p->rssi;
  buf[3] = (uint8_t)p->snr_q2;
  return PAYLOAD_ACK_SIZE;
}

size_t payload_ack_get(const uint8_t *buf, size_t len, payload_ack_t *p) {
  if (!buf || !p || len < PAYLOAD_ACK_SIZE) return 0;
  p->ack_seq = get_u16(&buf[0]);
  p->rssi = (int8_t)buf[2];
  p->snr_q2 = (int8_t)buf[3];
  return PAYLOAD_ACK_SIZE;
}

size_t payload_health_put(uint8_t *buf, size_t cap, const payload_health_t *p) {
  if (!buf || !p || cap < PAYLOAD_HEALTH_SIZE) return 0;
  put_u16(&buf[0], p->fw_hash);
  // Uptime is three bytes: 194 days of seconds, which outlives any deployment
  // between service visits, and saves a byte over a u32.
  buf[2] = (uint8_t)(p->uptime_s & 0xFFu);
  buf[3] = (uint8_t)((p->uptime_s >> 8) & 0xFFu);
  buf[4] = (uint8_t)((p->uptime_s >> 16) & 0xFFu);
  buf[5] = p->reboots;
  buf[6] = p->last_crash;
  buf[7] = p->vbat_dv;
  put_u16(&buf[8], (uint16_t)p->last_rssi);
  put_u16(&buf[10], p->post_mask);
  return PAYLOAD_HEALTH_SIZE;
}

size_t payload_health_get(const uint8_t *buf, size_t len, payload_health_t *p) {
  if (!buf || !p || len < PAYLOAD_HEALTH_SIZE) return 0;
  p->fw_hash = get_u16(&buf[0]);
  p->uptime_s = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8) | ((uint32_t)buf[4] << 16);
  p->reboots = buf[5];
  p->last_crash = buf[6];
  p->vbat_dv = buf[7];
  p->last_rssi = (int16_t)get_u16(&buf[8]);
  p->post_mask = get_u16(&buf[10]);
  return PAYLOAD_HEALTH_SIZE;
}
