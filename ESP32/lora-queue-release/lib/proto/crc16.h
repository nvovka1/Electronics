#pragma once

#include <stddef.h>
#include <stdint.h>

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
// Check value for the ASCII string "123456789" is 0x29B1.
//
// This catches random corruption in the channel and does it well. It is not a
// signature: the algorithm is public, so anyone who changes a byte can
// recompute it. Authenticity would need a MAC over a shared key.
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

// Incremental form, for hashing a header and a payload that are not adjacent
// in memory. Seed the first call with 0xFFFF.
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len);
