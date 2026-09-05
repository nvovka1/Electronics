#pragma once

#include <stddef.h>
#include <stdint.h>

// CRC-32/ISO-HDLC (the zlib/PNG one): poly 0xEDB88320 reflected, init
// 0xFFFFFFFF, final XOR 0xFFFFFFFF. Check value for "123456789" is 0xCBF43926.
//
// Used to tell a fully written config record from one that was interrupted by
// a power cut. A torn record almost never carries a matching CRC.
uint32_t crc32_iso(const uint8_t *data, size_t len);
