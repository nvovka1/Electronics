#include "snapshot.h"

#include <string.h>

#include <common/mavlink.h>

// MAVLink's own "no data" markers. Each one is the largest value the field can
// hold, so a naive reader sees a plausible-looking number rather than nothing -
// which is why every one of them is checked here and turned into our own
// unknown before it can reach a row.
static const uint16_t MavUnknownU16 = UINT16_MAX;
static const uint8_t MavUnknownU8 = UINT8_MAX;

void snapshotReset(TelemetrySnapshot &snapshot) {
  memset(&snapshot, 0, sizeof(snapshot));

  snapshot.unixTimeUs = 0;
  snapshot.haveHeartbeat = false;
  snapshot.armed = false;

  snapshot.gpsFix = UNKNOWN_I8;
  snapshot.sats = UNKNOWN_I8;
  snapshot.hdop = NAN;
  snapshot.lat = NAN;
  snapshot.lon = NAN;
  snapshot.altMsl = NAN;
  snapshot.altRel = NAN;
  snapshot.groundSpeed = NAN;
  snapshot.airSpeed = NAN;
  snapshot.climb = NAN;
  snapshot.heading = NAN;
  snapshot.cog = NAN;
  snapshot.roll = NAN;
  snapshot.pitch = NAN;
  snapshot.yaw = NAN;
  snapshot.throttle = UNKNOWN_I16;

  snapshot.batteryVoltage = NAN;
  snapshot.batteryCurrent = NAN;
  snapshot.batteryRemaining = UNKNOWN_I16;
  snapshot.consumedMah = UNKNOWN_I32;

  snapshot.rcRssi = UNKNOWN_I16;
  snapshot.wifiRssi = UNKNOWN_I16;
}

uint32_t snapshotLinkAgeMs(const TelemetrySnapshot &snapshot, uint32_t nowMs) {
  if (!snapshot.haveHeartbeat) return UINT32_MAX;
  return nowMs - snapshot.lastHeartbeatMs;
}

// True while the filtered position estimate is still arriving. GPS_RAW_INT
// defers to it for as long as this holds.
static bool globalPositionIsFresh(const TelemetrySnapshot &snapshot, uint32_t nowMs) {
  if (snapshot.globalPositionAtMs == 0) return false;
  return (nowMs - snapshot.globalPositionAtMs) < 3000;
}

void snapshotApply(TelemetrySnapshot &snapshot, const void *message, uint32_t nowMs) {
  const mavlink_message_t *msg = static_cast<const mavlink_message_t *>(message);

  snapshot.messagesSeen++;

  switch (msg->msgid) {
    case MAVLINK_MSG_ID_HEARTBEAT: {
      // Only the autopilot's. A companion computer, a gimbal or our own
      // heartbeat echoed back would otherwise keep the link looking alive after
      // the flight controller had stopped talking, which is the one thing the
      // link age exists to reveal.
      if (msg->compid != MAV_COMP_ID_AUTOPILOT1) break;

      mavlink_heartbeat_t heartbeat;
      mavlink_msg_heartbeat_decode(msg, &heartbeat);

      snapshot.haveHeartbeat = true;
      snapshot.lastHeartbeatMs = nowMs;
      snapshot.armed = (heartbeat.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
      snapshot.mode = heartbeat.custom_mode;
      snapshot.systemStatus = heartbeat.system_status;
      break;
    }

    case MAVLINK_MSG_ID_SYS_STATUS: {
      mavlink_sys_status_t status;
      mavlink_msg_sys_status_decode(msg, &status);

      if (status.voltage_battery != MavUnknownU16) {
        snapshot.batteryVoltage = status.voltage_battery / 1000.0f;
      }
      if (status.current_battery != -1) {
        // Centiamps, and signed: a negative current is charge flowing back in,
        // which a regenerating motor really can produce.
        snapshot.batteryCurrent = status.current_battery / 100.0f;
      }
      if (status.battery_remaining != -1) {
        snapshot.batteryRemaining = status.battery_remaining;
      }
      break;
    }

    case MAVLINK_MSG_ID_BATTERY_STATUS: {
      mavlink_battery_status_t battery;
      mavlink_msg_battery_status_decode(msg, &battery);

      if (battery.current_consumed != -1) {
        snapshot.consumedMah = battery.current_consumed;
      }
      // Only as a fallback. SYS_STATUS reports the battery the autopilot has
      // chosen to monitor; this message reports one particular battery, and on
      // a single-battery aircraft they agree.
      if (isnan(snapshot.batteryVoltage) && battery.voltages[0] != MavUnknownU16) {
        snapshot.batteryVoltage = battery.voltages[0] / 1000.0f;
      }
      break;
    }

    case MAVLINK_MSG_ID_SYSTEM_TIME: {
      mavlink_system_time_t systemTime;
      mavlink_msg_system_time_decode(msg, &systemTime);

      // Zero until the GPS has a fix. Accepting it would stamp the whole flight
      // as 1970, which sorts wrongly and looks like a bug forever after.
      if (systemTime.time_unix_usec != 0) {
        snapshot.unixTimeUs = systemTime.time_unix_usec;
        snapshot.unixTimeAtMs = nowMs;
      }
      break;
    }

    case MAVLINK_MSG_ID_GPS_RAW_INT: {
      mavlink_gps_raw_int_t gps;
      mavlink_msg_gps_raw_int_decode(msg, &gps);

      snapshot.gpsFix = (int8_t)gps.fix_type;
      if (gps.satellites_visible != MavUnknownU8) {
        snapshot.sats = (int8_t)gps.satellites_visible;
      }
      if (gps.eph != MavUnknownU16) {
        snapshot.hdop = gps.eph / 100.0f;
      }
      if (gps.cog != MavUnknownU16) {
        snapshot.cog = gps.cog / 100.0f;
      }

      if (!globalPositionIsFresh(snapshot, nowMs)) {
        snapshot.lat = gps.lat / 1e7;
        snapshot.lon = gps.lon / 1e7;
        snapshot.altMsl = gps.alt / 1000.0f;
      }
      break;
    }

    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
      mavlink_global_position_int_t position;
      mavlink_msg_global_position_int_decode(msg, &position);

      snapshot.globalPositionAtMs = nowMs;
      snapshot.lat = position.lat / 1e7;
      snapshot.lon = position.lon / 1e7;
      snapshot.altMsl = position.alt / 1000.0f;
      snapshot.altRel = position.relative_alt / 1000.0f;

      if (position.hdg != MavUnknownU16) {
        snapshot.heading = position.hdg / 100.0f;
      }
      break;
    }

    case MAVLINK_MSG_ID_VFR_HUD: {
      mavlink_vfr_hud_t hud;
      mavlink_msg_vfr_hud_decode(msg, &hud);

      snapshot.groundSpeed = hud.groundspeed;
      snapshot.airSpeed = hud.airspeed;
      snapshot.climb = hud.climb;
      snapshot.throttle = (int16_t)hud.throttle;

      // Whole degrees here against centidegrees in GLOBAL_POSITION_INT, so the
      // filtered one wins whenever it is arriving.
      if (!globalPositionIsFresh(snapshot, nowMs)) {
        snapshot.heading = hud.heading;
      }
      break;
    }

    case MAVLINK_MSG_ID_ATTITUDE: {
      mavlink_attitude_t attitude;
      mavlink_msg_attitude_decode(msg, &attitude);

      const float toDegrees = 57.2957795f;
      snapshot.roll = attitude.roll * toDegrees;
      snapshot.pitch = attitude.pitch * toDegrees;
      snapshot.yaw = attitude.yaw * toDegrees;
      break;
    }

    case MAVLINK_MSG_ID_RC_CHANNELS: {
      mavlink_rc_channels_t channels;
      mavlink_msg_rc_channels_decode(msg, &channels);

      if (channels.rssi != MavUnknownU8) {
        snapshot.rcRssi = channels.rssi;
      }
      break;
    }

    default:
      // Everything else is counted and dropped. The stream carries a great deal
      // this logger has no use for, and listing it here would only invite it in.
      break;
  }
}
