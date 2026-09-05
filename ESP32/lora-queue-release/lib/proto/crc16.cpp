#include "crc16.h"

// Bitwise rather than table-driven: a 512-byte table is not worth it for
// frames of at most 58 bytes, and this version has no initialisation order to
// get wrong.
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
  }
  return crc;
}

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len) {
  return crc16_update(0xFFFFu, data, len);
}
