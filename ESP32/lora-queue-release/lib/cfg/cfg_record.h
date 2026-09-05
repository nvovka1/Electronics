#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The on-flash container for a config blob, and the rule for choosing between
// two of them.
//
// The device keeps two records, A and B, and always writes to the one that is
// NOT currently active. This is the OTA two-slot rule applied to settings: at
// the moment of a write there is always a complete, valid record on the
// device. A power cut mid-write leaves the target slot either torn (its CRC
// fails) or still carrying the older sequence number, and either way the
// loader keeps using the previous good one.

#define CFG_RECORD_MAGIC 0x3143514Cu // "LQC1"
#define CFG_BLOB_MAX 64

typedef struct {
  uint32_t magic;
  uint32_t seq; // monotonic write counter; the higher valid one wins
  uint16_t size; // meaningful bytes in blob
  uint16_t cfg_version;
  uint8_t blob[CFG_BLOB_MAX];
  uint32_t crc32; // over every byte of this struct before it
} cfg_record_t;

// The whole record is zeroed before it is filled, so the unused tail of blob
// is a known value and the CRC over a given config is reproducible.
void cfg_record_build(cfg_record_t *r, uint32_t seq, uint16_t cfg_version,
                      const void *blob, uint16_t size);

bool cfg_record_valid(const cfg_record_t *r);

typedef enum {
  CFG_SLOT_NONE = -1,
  CFG_SLOT_A = 0,
  CFG_SLOT_B = 1,
} cfg_slot_t;

// Picks the valid record with the higher sequence number. A tie goes to A,
// so the choice is deterministic rather than merely arbitrary.
cfg_slot_t cfg_record_pick(const cfg_record_t *a, bool a_present,
                           const cfg_record_t *b, bool b_present);

// The slot the next write must target: never the active one.
cfg_slot_t cfg_record_next_slot(cfg_slot_t active);

const char *cfg_slot_name(cfg_slot_t slot);
