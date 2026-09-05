#pragma once

#include <Arduino.h>

// Build identity. Every value here arrives as a -D flag from
// scripts/version_flags.py, which reads it out of git. Nothing in this file is
// ever edited by hand: a version typed into a header is forgotten by the
// second release, and then "1.0.0" exists in five different variants.
//
// The fallbacks below only ever apply to a build made outside a git checkout.

#ifndef FW_SEMVER
#define FW_SEMVER "0.0.0"
#endif

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "nogit00"
#endif

#ifndef FW_BUILD_UTC
#define FW_BUILD_UTC "unknown"
#endif

#ifndef FW_GIT_DIRTY
#define FW_GIT_DIRTY 1 // assume the worst: an unidentified build is not clean
#endif

#ifndef FW_HASH_U32
#define FW_HASH_U32 0u
#endif

#ifndef FW_BUILD_TYPE
#define FW_BUILD_TYPE "unknown"
#endif

#ifndef PROTO_VERSION
#define PROTO_VERSION 1
#endif

// Semver says what is compatible; the hash says which code it actually is.
// Both are needed, so both are printed together.
#define FW_HARDWARE "TTGO-LoRa32 v2.1 (PICO-D4)"

// "1.0.0+3f9a1c7" or "1.0.0+3f9a1c7-dirty". An image with the -dirty suffix
// never goes to the field: there is no commit to go back to.
const char *fwVersionString();

// Low 16 bits of the git hash. Health telemetry has two bytes for this, and
// they are enough to see at a glance who has not updated yet.
uint16_t fwHash16();

bool fwIsDirty();
