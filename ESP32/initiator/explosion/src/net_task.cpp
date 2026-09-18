#include "net_task.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "event_buffer.h"
#include "fleet_client.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"
#include "state_task.h"
#include "ui_task.h"

namespace {

bool connected = false;
bool triedOnce = false;

// Set at start-up when the previous reset was a brownout. While it is true this
// task never switches the radio on.
//
// Without it the board is in a loop nobody can break: WiFi starts, the supply
// sags, the chip resets, and it starts again a hundred milliseconds later -
// which does not even leave a window to type into the shell and turn it off.
// Coming back with the radio down is the only state from which the problem can
// be looked at.
bool wifiHeldOff = false;

// Consecutive brownout resets, kept across resets in RTC memory.
//
// RTC_NOINIT_ATTR, not RTC_DATA_ATTR. The difference is the whole mechanism:
// the .rtc.data section is reloaded from the image on every reset, so a counter
// there is zero on each boot and never reaches a threshold - which is exactly
// how this was broken, silently, while looking correct.
//
// .rtc.noinit is left alone, so it survives a reset. It is indeterminate after
// a power cut, which is what the magic word is for: it tells "this is our
// counter" apart from "this is whatever was in that RAM cell".
//
// RTC only, never NVS. A board that is browning out is the worst possible place
// to be writing flash, and the question this answers - "has the supply already
// failed since it was last plugged in" - is one a power cycle should reset,
// because unplugging is also the gesture that means somebody just changed the
// cable or fitted a capacitor.
constexpr uint32_t RtcMagic = 0x494E4954u; // "INIT"

RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint8_t rtcBrownoutStreak;

uint8_t brownoutStreak = 0;

uint32_t lastHealthMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastWifiAttemptMs = 0;

// True once `since` has elapsed. Signed arithmetic, so it stays correct across
// the millis() wrap at 49 days rather than going quiet for the next 49.
bool elapsed(uint32_t since, uint32_t interval) {
  return (int32_t)(millis() - since) >= (int32_t)interval;
}

void ensureWifi() {
  if (!settings.wifiEnabled) return;

  // The hold expires. Everything else about it is unchanged - the point was
  // only ever to break the reboot loop long enough for the board to be usable,
  // and one more attempt a minute later is how it finds out that somebody has
  // moved it to a better supply.
  if (wifiHeldOff) {
    if (millis() < BrownoutHoldMs) return;

    wifiHeldOff = false;
    LOG_WARN(TagNet, CodeWifiRetryAfterHold, brownoutStreak);
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      connected = true;
      LOG_INFO(TagNet, CodeWifiUp, WiFi.RSSI());

      // Read back from the driver rather than reported from the constant, so
      // the log says what the radio is actually doing. A setting that silently
      // failed to apply is exactly the bug this is here to catch.
      LOG_INFO(TagNet, CodeWifiTxPower, (int32_t)WiFi.getTxPower() / 4);

      uiSetWifiUp(true);
    }
    return;
  }

  if (connected) {
    connected = false;
    LOG_WARN(TagNet, CodeWifiDown, 0);
    uiSetWifiUp(false);
  }

  // The first attempt goes immediately. Gating it on the retry interval meant
  // the node sat doing nothing for fifteen seconds after every boot and only
  // then switched the radio on - which also made the resulting brownout look
  // like a delayed fault rather than like "WiFi just started".
  if (triedOnce && !elapsed(lastWifiAttemptMs, WifiRetryIntervalMs)) return;

  triedOnce = true;
  lastWifiAttemptMs = millis();

  if (settings.ssid[0] == '\0') return; // nothing to connect to

  // WHERE THE BROWNOUT ACTUALLY HAPPENS
  //
  // Not during transmission - inside WiFi.mode(), when the driver powers up the
  // RF front end and calibrates it. That is why lowering the transmit power
  // changed nothing however low it went: setTxPower cannot be applied until the
  // driver is running, and by then the spike that kills the board has already
  // happened.
  //
  // So the lever is everything ELSE that is drawing at that moment. Three
  // things stand down together, worth roughly 55 mA out of a burst of about
  // 250:
  //
  //   processor  240 -> 80 MHz     ~30 mA
  //   display    blanked           ~15 mA
  //   LoRa       receive -> sleep  ~12 mA
  //
  // That will not rescue a genuinely bad supply - a cable that cannot deliver
  // 250 mA usually cannot deliver 195 either - but it is enough on a board that
  // is only just failing. It costs nothing on a healthy one, because it is only
  // done after a brownout has already happened.
  const bool easeOff = brownoutStreak > 0;

  if (easeOff) {
    uiSuspendPanel();
    radioStandDown();
    Serial.flush(); // the frequency switch reprograms the UART divider mid-character
    setCpuFrequencyMhz(80);
  }

  WiFi.mode(WIFI_STA);

  // Modem sleep is deliberately NOT enabled. It was, while chasing the
  // brownout, and it is one more thing between this node and a working
  // connection - the project that already talks to this service does not use
  // it. Worth revisiting for battery life once reporting is reliable.

  // After mode(), because the driver has to exist to accept it. This bounds the
  // transmit burst, which is the larger spike but the later one.
  WiFi.setTxPower((wifi_power_t)(settings.wifiTxPowerDbm * 4));

  WiFi.begin(settings.ssid, settings.password);

  // Given back once the association attempt is under way. Held through begin()
  // rather than released immediately, because association is where the radio
  // first transmits.
  if (easeOff) {
    vTaskDelay(pdMS_TO_TICKS(EaseOffHoldMs));
    Serial.flush();
    setCpuFrequencyMhz(240);
    radioStandUp();
    uiResumePanel();
  }
}

// Drains the buffer in batches. Only what the service says it stored is
// dropped: assuming the whole batch would silently lose any record it could not
// understand.
void flushEvents() {
  StateEventRecord batch[MaxEventsPerUpload];

  while (eventBufferPending() > 0) {
    const uint8_t taken = eventBufferPeek(batch, MaxEventsPerUpload);
    if (taken == 0) break;

    uint8_t stored = 0;
    if (!fleetPostStateEvents(batch, taken, &stored)) {
      // Left in the buffer, to go again next time round. This is the whole
      // point of peek-then-commit.
      LOG_WARN(TagNet, CodeEventBuffered, eventBufferPending());
      return;
    }

    // The whole batch is dropped, not just the part that was stored. The
    // service has seen all of them; any it did not keep carried a value it
    // could not understand, and sending those again would loop forever. The
    // shortfall is logged so it is a known loss rather than a silent one.
    eventBufferCommit(taken);

    if (stored < taken) {
      LOG_WARN(TagNet, CodeEventBuffered, (int32_t)(taken - stored));
    }
  }
}

void flushLogs() {
  LogRecord batch[MaxLogsPerUpload];

  const uint8_t taken = logTake(batch, MaxLogsPerUpload);
  if (taken == 0) return;

  // Logs are taken rather than peeked: unlike a transition, a lost log line is
  // not part of the record of what the node did, and holding them through an
  // outage would push out newer ones that matter more.
  fleetPostLogs(batch, taken);
}

void pollForCommand() {
  PolledCommand polled;
  if (!fleetPollCommand(&polled)) return;

  CommandRequest request = {};
  request.command = polled.command;
  request.source = (uint8_t)SOURCE_API;
  strncpy(request.commandId, polled.commandId, COMMAND_ID_MAX - 1);

  // Onto the same queue the radio uses, so the two sources serialise and the
  // state machine judges this one against whatever state the node is actually
  // in by the time it is read.
  stateSubmitCommand(request);
}

void netTask(void *) {
  uint32_t lastBeatMs = 0;

  for (;;) {
    // A heartbeat, with the stack headroom beside it.
    //
    // Added because "the request went out and nothing came back, ever" has two
    // completely different causes and they look identical from outside: the
    // call is still running, or the task is gone. A beat that keeps arriving
    // says the first; a beat that stops at the same moment as the request says
    // the second. The high-water mark is there because the commonest way for a
    // task to vanish inside TLS is running out of stack.
    if (elapsed(lastBeatMs, NetHeartbeatMs)) {
      lastBeatMs = millis();
      LOG_INFO(TagNet, CodeNetAlive, (int32_t)uxTaskGetStackHighWaterMark(nullptr));
    }

    ensureWifi();

    if (connected) {
      if (elapsed(lastHealthMs, HealthReportIntervalMs)) {
        lastHealthMs = millis();

        uint8_t serviceState = stateCurrent();
        if (fleetPostHealth(stateCurrent(), &serviceState) &&
            serviceState != stateCurrent()) {
          // The service and this node disagree. The node is right - it holds
          // the state - so this is only worth a line, and the next state event
          // or report settles it.
          LOG_WARN(TagNet, CodeReportOk, logPack(stateCurrent(), serviceState));
        }

        // Straight to the screen, whatever it was. This is the line that says
        // whether the node is talking to the service, and on this board it is
        // the only place it can be read.
        uiSetApiStatus(fleetLastHttpStatus());

        flushEvents();
        flushLogs();
      }

      if (elapsed(lastPollMs, CommandPollIntervalMs)) {
        lastPollMs = millis();
        pollForCommand();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

} // namespace

void netTaskStart() {
  // Held off for THIS boot only, and only after an actual brownout. A RAM flag
  // on purpose: nothing is written to NVS, so the moment the supply is fixed
  // and the board starts cleanly, WiFi comes back on its own with nothing to
  // remember or undo.
  const esp_reset_reason_t reason = esp_reset_reason();

  // After a power cut the RTC cells hold whatever they hold, so the count is
  // only believed when the magic word is beside it.
  if (rtcMagic != RtcMagic) {
    rtcMagic = RtcMagic;
    rtcBrownoutStreak = 0;
  }

  if (reason == ESP_RST_BROWNOUT) {
    if (rtcBrownoutStreak < 255) rtcBrownoutStreak++;
  } else if (reason == ESP_RST_POWERON || reason == ESP_RST_EXT) {
    // Somebody has been at the hardware. Give the supply the benefit of the
    // doubt - they may well have just fitted the capacitor this counter exists
    // to ask for.
    rtcBrownoutStreak = 0;
  }

  brownoutStreak = rtcBrownoutStreak;

  // One brownout is worth another try with everything else stood down - that is
  // often enough. Two says the supply simply cannot do this, and a third
  // attempt only costs another reboot; the node is more useful spending it
  // answering questions over the cable.
  wifiHeldOff = brownoutStreak >= BrownoutHoldAt;

  if (wifiHeldOff) LOG_ERROR(TagNet, CodeWifiHeldOff, brownoutStreak);

  if (xTaskCreate(netTask, "net", NetTaskStack, nullptr, NetTaskPriority, nullptr) !=
      pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

void netEnableWifi() {
  wifiHeldOff = false;
  triedOnce = false; // so the next pass connects immediately rather than waiting
}

bool netWifiHeldOff() { return wifiHeldOff; }

bool netIsConnected() { return connected; }
