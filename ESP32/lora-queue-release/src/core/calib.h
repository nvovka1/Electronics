#pragma once

#include <Arduino.h>

// Calibration and identity: everything that is a property of this particular
// board rather than a setting on it.
//
// It lives in its own NVS namespace so that `config reset` cannot reach it.
// Thresholds can be restored from the firmware's defaults; a divider ratio
// measured against a reference, and the serial number printed on the case,
// cannot be recreated in the field.

bool calibBegin();

// "LQ-A41C". Public by design: it is printed on the case, it travels in every
// frame as SRC's human-readable twin, and it is the key into the build record.
// There is no secret in it.
const char *calibSerial();

// Q10 fixed point, 1024 = x1.000. Multiplies the nominal divider ratio.
uint16_t calibVbatScaleQ10();

// Both are writable only from the factory image. In dev and field builds these
// are not compiled at all, so nothing on a deployed node can overwrite the one
// number that cannot be measured again without the reference.
#if BUILD_PROVISIONING
bool calibSetSerial(const char *serial);
bool calibSetVbatScale(uint16_t scaleQ10);
#endif

// True when the serial came out of NVS rather than being derived from the chip
// MAC. A node that was never provisioned still has a stable name, but the
// distinction matters when a build record is being looked up.
bool calibIsProvisioned();
