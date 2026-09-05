#include "cfg_record.h"

#include <string.h>

#include "crc32.h"

static size_t crc_span(void) { return offsetof(cfg_record_t, crc32); }

void cfg_record_build(cfg_record_t *r, uint32_t seq, uint16_t cfg_version,
                      const void *blob, uint16_t size) {
  if (!r) return;
  memset(r, 0, sizeof(*r));
  if (size > CFG_BLOB_MAX) size = CFG_BLOB_MAX;

  r->magic = CFG_RECORD_MAGIC;
  r->seq = seq;
  r->size = size;
  r->cfg_version = cfg_version;
  if (blob && size) memcpy(r->blob, blob, size);

  r->crc32 = crc32_iso((const uint8_t *)r, crc_span());
}

bool cfg_record_valid(const cfg_record_t *r) {
  if (!r) return false;
  if (r->magic != CFG_RECORD_MAGIC) return false;
  if (r->size == 0 || r->size > CFG_BLOB_MAX) return false;
  return r->crc32 == crc32_iso((const uint8_t *)r, crc_span());
}

cfg_slot_t cfg_record_pick(const cfg_record_t *a, bool a_present,
                           const cfg_record_t *b, bool b_present) {
  const bool a_ok = a_present && cfg_record_valid(a);
  const bool b_ok = b_present && cfg_record_valid(b);

  if (a_ok && b_ok) return (b->seq > a->seq) ? CFG_SLOT_B : CFG_SLOT_A;
  if (a_ok) return CFG_SLOT_A;
  if (b_ok) return CFG_SLOT_B;
  return CFG_SLOT_NONE;
}

cfg_slot_t cfg_record_next_slot(cfg_slot_t active) {
  return (active == CFG_SLOT_A) ? CFG_SLOT_B : CFG_SLOT_A;
}

const char *cfg_slot_name(cfg_slot_t slot) {
  switch (slot) {
    case CFG_SLOT_A: return "A";
    case CFG_SLOT_B: return "B";
    default: return "-";
  }
}
