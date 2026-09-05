#include "core/health.h"

#include "app/safe_mode.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "tasks/radio_task.h"

payload_health_t healthSnapshot() {
  payload_health_t h;

  h.fw_hash = fwHash16();
  h.uptime_s = millis() / 1000u;
  h.reboots = (uint8_t)(totalBootCount() > 255 ? 255 : totalBootCount());
  h.last_crash = lastCrashCode();
  h.vbat_dv = batteryTrusted() ? batteryDeciVolts() : 0; // 0 means "no reading"
  h.last_rssi = (int16_t)radioLastRssi();
  h.post_mask = postMask();

  return h;
}

void healthPrint(Print &out) {
  const payload_health_t h = healthSnapshot();

  out.printf("fw_hash   0x%04X\n", h.fw_hash);
  out.printf("uptime    %lu s\n", (unsigned long)h.uptime_s);
  out.printf("reboots   %u  last: %s\n", h.reboots, lastResetReasonName());
  out.printf("crash     %u\n", h.last_crash);
  if (h.vbat_dv)
    out.printf("vbat      %u.%u V\n", h.vbat_dv / 10, h.vbat_dv % 10);
  else
    out.println("vbat      untrusted");
  out.printf("rssi      %d dBm\n", h.last_rssi);
  out.printf("post      0x%04X\n", h.post_mask);
}
