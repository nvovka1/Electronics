#include "core/version.h"

#include <stdio.h>

const char *fwVersionString() {
  static char buf[48];
  static bool built = false;
  if (!built) {
    snprintf(buf, sizeof(buf), "%s+%s%s", FW_SEMVER, FW_GIT_HASH,
             FW_GIT_DIRTY ? "-dirty" : "");
    built = true;
  }
  return buf;
}

uint16_t fwHash16() { return (uint16_t)(FW_HASH_U32 & 0xFFFFu); }

bool fwIsDirty() { return FW_GIT_DIRTY != 0; }
