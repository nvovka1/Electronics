#include "mav_task.h"

#include <Arduino.h>
#include <common/mavlink.h>

#include "board_pins.h"
#include "config.h"
#include "log.h"
#include "settings.h"
#include "store.h"

static HardwareSerial _link(MavUartNumber);

static uint32_t _messagesSeen = 0;
static uint32_t _parseErrors = 0;
static uint32_t _lastHeartbeatMs = 0;
static bool _haveHeartbeat = false;

// Learned from the first heartbeat rather than assumed. ArduPilot is system 1
// by default, but a board someone has renumbered would otherwise be sent
// commands addressed to a system that is not listening, and the symptom - the
// default stream rates, quietly - looks nothing like the cause.
static uint8_t _targetSystem = 0;
static uint8_t _targetComponent = 0;

static void send(const mavlink_message_t &message) {
  uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
  const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
  _link.write(buffer, length);
}

static void sendHeartbeat() {
  mavlink_message_t message;
  mavlink_msg_heartbeat_pack(MavOurSystemId, MavOurComponentId, &message, MAV_TYPE_GCS,
                             MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
  send(message);
}

static void requestInterval(uint32_t messageId, uint32_t hertz) {
  if (_targetSystem == 0) return;

  const float intervalUs = hertz == 0 ? -1.0f : 1000000.0f / (float)hertz;

  mavlink_message_t message;
  mavlink_msg_command_long_pack(MavOurSystemId, MavOurComponentId, &message, _targetSystem,
                                _targetComponent, MAV_CMD_SET_MESSAGE_INTERVAL, 0,
                                (float)messageId, intervalUs, 0, 0, 0, 0, 0);
  send(message);
}

// Asking for exactly what the columns need, at the rate the logger will read
// them. Requesting more than the log rate wastes link bandwidth the RC failsafe
// may want; requesting less means consecutive rows repeat values, which reads
// as an aircraft that stopped moving.
static void requestStreams() {
  const uint32_t fastHz = settings.logRateHz;

  requestInterval(MAVLINK_MSG_ID_ATTITUDE, fastHz);
  requestInterval(MAVLINK_MSG_ID_GLOBAL_POSITION_INT, fastHz);
  requestInterval(MAVLINK_MSG_ID_VFR_HUD, fastHz);
  requestInterval(MAVLINK_MSG_ID_SYS_STATUS, fastHz);
  requestInterval(MAVLINK_MSG_ID_GPS_RAW_INT, fastHz);

  // Once a second is plenty. The consumed-mAh total and the RC signal strength
  // do not move fast enough to be worth more, and the wall clock moves at a
  // known rate whether or not anybody reports it.
  requestInterval(MAVLINK_MSG_ID_BATTERY_STATUS, 1);
  requestInterval(MAVLINK_MSG_ID_RC_CHANNELS, 1);
  requestInterval(MAVLINK_MSG_ID_SYSTEM_TIME, 1);

  LOG_INFO("mav", "streams requested from sys=%u comp=%u at %luHz", (unsigned)_targetSystem,
           (unsigned)_targetComponent, (unsigned long)fastHz);
}

// The flight controller's own words. Not a CSV column - it is prose, arrives
// when it likes, and matters most when it says something went wrong - so it
// goes to the log, where a person reading the board's page will find it next to
// whatever this firmware was doing at the time.
static void handleStatusText(const mavlink_message_t &message) {
  mavlink_statustext_t statusText;
  mavlink_msg_statustext_decode(&message, &statusText);

  char text[51];
  memcpy(text, statusText.text, 50);
  text[50] = 0;

  const bool bad = statusText.severity <= MAV_SEVERITY_ERROR;
  logPrintf(bad ? LevelError : LevelInfo, "fc", "%s", text);
}

static void mavTask(void *) {
  _link.setRxBufferSize(2048);
  _link.begin(settings.mavBaud, SERIAL_8N1, MavRxPin, MavTxPin);

  LOG_INFO("mav", "uart%d rx=%d tx=%d @%lu", MavUartNumber, MavRxPin, MavTxPin,
           (unsigned long)settings.mavBaud);

  mavlink_message_t message;
  mavlink_status_t status;

  uint32_t lastOurHeartbeatMs = 0;
  uint32_t lastRequestMs = 0;

  for (;;) {
    const uint32_t now = millis();

    // Draining comes first, always. Everything else in this loop can wait a
    // few milliseconds; a full receive buffer cannot, because the bytes it
    // drops are in the middle of frames and cost the frames either side too.
    while (_link.available() > 0) {
      const uint8_t byte = (uint8_t)_link.read();

      if (mavlink_parse_char(MAVLINK_COMM_0, byte, &message, &status) != MAVLINK_FRAMING_OK) {
        continue;
      }

      _messagesSeen++;
      _parseErrors = status.packet_rx_drop_count;

      if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT &&
          message.compid == MAV_COMP_ID_AUTOPILOT1) {
        _lastHeartbeatMs = now;

        if (!_haveHeartbeat) {
          _haveHeartbeat = true;
          _targetSystem = message.sysid;
          _targetComponent = message.compid;
          LOG_INFO("mav", "link up, sys=%u", (unsigned)_targetSystem);
          requestStreams();
          lastRequestMs = now;
        }
      }

      if (message.msgid == MAVLINK_MSG_ID_STATUSTEXT) handleStatusText(message);

      storeApplyMessage(&message, now);
    }

    if (now - lastOurHeartbeatMs >= MavHeartbeatIntervalMs) {
      lastOurHeartbeatMs = now;
      sendHeartbeat();
    }

    // Re-asked periodically because ArduPilot forgets the intervals across its
    // own reboot. A flight controller that restarts mid-session would otherwise
    // fall back to its SR*_ parameters and quietly halve the log's resolution.
    if (_haveHeartbeat && now - lastRequestMs >= MavRequestIntervalMs) {
      lastRequestMs = now;
      requestStreams();
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void mavTaskStart() {
  xTaskCreatePinnedToCore(mavTask, "mav", 4096, nullptr, 3, nullptr, 1);
}

uint32_t mavLinkAgeMs() {
  if (!_haveHeartbeat) return UINT32_MAX;
  return millis() - _lastHeartbeatMs;
}

uint32_t mavMessagesSeen() { return _messagesSeen; }
uint32_t mavParseErrors() { return _parseErrors; }
