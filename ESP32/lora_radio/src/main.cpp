// LoRa band scanner for the LILYGO T3 v1.6.1 (868 MHz).
//
// Listens on the 868 MHz ISM band and shows what it hears on the on-board OLED:
// live signal strength, decoded packets, an RSSI graph over time, and a noise
// floor sweep across the whole band. An analog joystick drives the whole UI —
// left/right tunes, up/down changes page — with the BOOT button as "click".
//
// Read WIRING.md before powering it up: the joystick must be fed from 3V3, not
// 5V, and an antenna must be attached before the transmit self-test.
//
//   Flash:    pio run -t upload
//   Monitor:  pio device monitor
//   Wiring:   pio run -e joystick-check -t upload
//
// This is a receive-only monitor. It is not a spectrum analyser and not a
// promiscuous sniffer — no LoRa chip can be either. See README.md.

#include <Arduino.h>

#include "battery.h"
#include "joystick.h"
#include "pins.h"
#include "radio.h"
#include "ui.h"

namespace {

constexpr uint32_t SplashHoldMs = 2500;
constexpr uint32_t PacketLedMs = 60;

uint32_t lastSeenPacketCount = 0;
uint32_t ledOffAt = 0;

// Heartbeat: one line every couple of seconds reporting that loop() is alive and
// how fast it is spinning. Cheap, and it distinguishes a hung loop from a render
// bug from a reboot loop without any guesswork.
constexpr uint32_t HeartbeatIntervalMs = 5000;
uint32_t heartbeatAt = 0;
uint32_t loopIterations = 0;

void stage(const char* name) {
    Serial.printf("[setup] %s\n", name);
    Serial.flush();
}

void logHeartbeat() {
    ++loopIterations;
    const uint32_t now = millis();
    if (now - heartbeatAt < HeartbeatIntervalMs) return;

    Serial.printf("[hb] up=%lus loops=%lu page=%u rf=%d rssi=%d stick=%d/%d\n",
                  static_cast<unsigned long>(now / 1000),
                  static_cast<unsigned long>(loopIterations),
                  static_cast<unsigned>(ui::page()), radio::healthy() ? 1 : 0,
                  radio::currentRssi(), joystick::rawX(), joystick::rawY());

    heartbeatAt = now;
    loopIterations = 0;
}

// Pulses the LED whenever a packet lands, so you can tell the radio is decoding
// without watching the screen.
void updatePacketLed() {
    const uint32_t count = radio::packetCount();
    if (count != lastSeenPacketCount) {
        lastSeenPacketCount = count;
        digitalWrite(LED_PIN, HIGH);
        ledOffAt = millis() + PacketLedMs;
    } else if (ledOffAt != 0 && millis() >= ledOffAt) {
        digitalWrite(LED_PIN, LOW);
        ledOffAt = 0;
    }
}

void adjustSelectedSetting(int delta) {
    switch (ui::settingsRow()) {
        case ui::SettingsRow::Preset:          radio::cyclePreset(delta); break;
        case ui::SettingsRow::SpreadingFactor: radio::adjustSpreadingFactor(delta); break;
        case ui::SettingsRow::Bandwidth:       radio::adjustBandwidth(delta); break;
        case ui::SettingsRow::CodingRate:      radio::adjustCodingRate(delta); break;
        case ui::SettingsRow::SyncWord:        radio::adjustSyncWord(delta); break;
        case ui::SettingsRow::StepSize:        radio::nextStepSize(delta); break;
        case ui::SettingsRow::Stick:           break;  // read-only diagnostic
        default: break;
    }
}

// Dialogs swallow input, so they are handled before anything else. Returns true
// when the event was consumed.
bool handleDialogEvent(JoystickEvent event) {
    if (ui::dialog() == ui::Dialog::None) return false;

    if (ui::dialog() == ui::Dialog::SelfTestConfirm) {
        if (event == JoystickEvent::Click) {
            ui::showDialog(radio::selfTest() ? ui::Dialog::SelfTestPassed
                                             : ui::Dialog::SelfTestFailed);
        } else if (event != JoystickEvent::None) {
            ui::showDialog(ui::Dialog::None);  // anything else cancels
        }
        return true;
    }

    // Result dialogs: any input dismisses.
    if (event != JoystickEvent::None) ui::showDialog(ui::Dialog::None);
    return true;
}

void handleHorizontal(int delta) {
    switch (ui::page()) {
        case ui::Page::Live:
        case ui::Page::Packet:
        case ui::Page::Signal:
            radio::tune(delta);
            break;
        case ui::Page::Sweep:
            radio::moveSweepCursor(delta);
            break;
        case ui::Page::Settings:
            adjustSelectedSetting(delta);
            break;
        default:
            break;
    }
}

void handleClick() {
    switch (ui::page()) {
        case ui::Page::Live:
            radio::resetStatistics();
            break;
        case ui::Page::Packet:
            ui::toggleHexEmphasis();
            break;
        case ui::Page::Signal:
            radio::clearRssiHistory();
            break;
        case ui::Page::Sweep:
            if (radio::sweeping()) {
                radio::stopSweep();
            } else {
                radio::startSweep();
            }
            break;
        case ui::Page::Settings:
            ui::moveSettingsRow(1);
            break;
        default:
            break;
    }
}

void handleEvent(JoystickEvent event) {
    if (event == JoystickEvent::None) return;
    if (handleDialogEvent(event)) return;

    switch (event) {
        case JoystickEvent::Up:    ui::changePage(-1); break;
        case JoystickEvent::Down:  ui::changePage(1); break;
        case JoystickEvent::Left:  handleHorizontal(-1); break;
        case JoystickEvent::Right: handleHorizontal(1); break;
        case JoystickEvent::Click: handleClick(); break;
        case JoystickEvent::LongClick:
            // Transmitting is the one thing here that touches the airwaves, so it
            // lives behind a long press on the settings page plus a confirmation.
            if (ui::page() == ui::Page::Settings) {
                ui::showDialog(ui::Dialog::SelfTestConfirm);
            }
            break;
        default:
            break;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\nLoRa band scanner starting");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Stage markers, flushed immediately. A watchdog reset kills the board without
    // unwinding anything, so the last marker printed is the call that hung.
    stage("joystick");
    joystick::begin();

    stage("battery");
    battery::begin();

    stage("display");
    if (!ui::begin()) {
        // No display means no UI, but the serial log still works, so keep going.
        Serial.println("Continuing without display");
    }

    stage("radio");
    const bool radioOk = radio::begin();
    radio::clearRssiHistory();

    stage("splash");

    ui::showSplash(radioOk, joystick::detected());
    delay(SplashHoldMs);

    stage("done - entering loop");
}

void loop() {
    battery::update();
    radio::poll();
    handleEvent(joystick::poll());
    updatePacketLed();
    ui::render();
    logHeartbeat();
}
