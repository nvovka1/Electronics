#include "joystick.h"

#include "pins.h"

namespace {

// ESP32 ADC is 12-bit and, at the Arduino default 11 dB attenuation, spans
// roughly 0-3.3 V. A joystick sitting at half its supply should therefore read
// near mid-scale.
constexpr int AdcMax = 4095;
constexpr int PlausibleCentreLow  = 1200;
constexpr int PlausibleCentreHigh = 2900;

// Deflection needed to trigger, and the smaller value it must fall back below
// before re-triggering. The gap is what stops a stick resting near the edge of
// the deadzone from chattering.
constexpr int TriggerOffset = 500;
constexpr int ReleaseOffset = 300;

// Hold an axis to repeat, the way a keyboard does.
constexpr uint32_t RepeatDelayMs    = 400;
constexpr uint32_t RepeatIntervalMs = 120;

constexpr uint32_t DebounceMs  = 30;
constexpr uint32_t LongPressMs = 700;

constexpr uint8_t OversampleReads = 8;
constexpr uint8_t CentreReads     = 32;
constexpr uint8_t DiscardedReads  = 8;

int centreXValue = AdcMax / 2;
int centreYValue = AdcMax / 2;
int lastXValue   = AdcMax / 2;
int lastYValue   = AdcMax / 2;
bool axesDetected = false;

// Per-axis repeat state. -1 / 0 / +1.
int8_t heldX = 0;
int8_t heldY = 0;
uint32_t heldXSince = 0;
uint32_t heldYSince = 0;
uint32_t heldXLastFire = 0;
uint32_t heldYLastFire = 0;

bool buttonPressed      = false;
bool longPressDelivered = false;
uint32_t buttonChangedAt = 0;
uint32_t buttonPressedAt = 0;

int readAxis(uint8_t pin) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < OversampleReads; ++i) {
        total += analogRead(pin);
    }
    return static_cast<int>(total / OversampleReads);
}

// Turns a raw axis reading into -1, 0 or +1, remembering the previous direction
// so the release threshold applies while deflected.
int8_t direction(int value, int centre, int8_t previous) {
    const int offset = value - centre;
    const int threshold = (previous != 0) ? ReleaseOffset : TriggerOffset;

    if (offset > threshold) return 1;
    if (offset < -threshold) return -1;
    return 0;
}

// Shared repeat logic for both axes: emits on the initial deflection, then again
// once the hold delay has elapsed, then at a steady interval.
bool shouldFire(int8_t current, int8_t& held, uint32_t& since, uint32_t& lastFire, uint32_t now) {
    if (current == 0) {
        held = 0;
        return false;
    }

    if (current != held) {
        held = current;
        since = now;
        lastFire = now;
        return true;
    }

    if (now - since < RepeatDelayMs) return false;
    if (now - lastFire < RepeatIntervalMs) return false;

    lastFire = now;
    return true;
}

}  // namespace

namespace joystick {

void begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    analogReadResolution(12);

    // The first few conversions after power-up are unreliable — they produced
    // occasional 0 and 3570 readings on an otherwise steady stick — so throw them
    // away before averaging rather than letting them drag the centre off.
    for (uint8_t i = 0; i < DiscardedReads; ++i) {
        analogRead(JOYSTICK_X_PIN);
        analogRead(JOYSTICK_Y_PIN);
        delay(2);
    }

    uint32_t totalX = 0;
    uint32_t totalY = 0;
    for (uint8_t i = 0; i < CentreReads; ++i) {
        totalX += analogRead(JOYSTICK_X_PIN);
        totalY += analogRead(JOYSTICK_Y_PIN);
        delay(2);
    }
    centreXValue = static_cast<int>(totalX / CentreReads);
    centreYValue = static_cast<int>(totalY / CentreReads);
    lastXValue = centreXValue;
    lastYValue = centreYValue;

    axesDetected = centreXValue > PlausibleCentreLow && centreXValue < PlausibleCentreHigh &&
                   centreYValue > PlausibleCentreLow && centreYValue < PlausibleCentreHigh;

    Serial.printf("Joystick centre X=%d Y=%d (%s)\n", centreXValue, centreYValue,
                  axesDetected ? "ok" : "IMPLAUSIBLE - check wiring");
}

JoystickEvent poll() {
    const uint32_t now = millis();

    // --- Button first, so a click is never starved by a held axis ---
    const bool down = digitalRead(BUTTON_PIN) == LOW;
    if (down != buttonPressed && now - buttonChangedAt > DebounceMs) {
        buttonPressed = down;
        buttonChangedAt = now;
        if (down) {
            buttonPressedAt = now;
            longPressDelivered = false;
        } else if (!longPressDelivered) {
            return JoystickEvent::Click;
        }
    }

    if (buttonPressed && !longPressDelivered && now - buttonPressedAt >= LongPressMs) {
        longPressDelivered = true;
        return JoystickEvent::LongClick;
    }

    // --- Axes ---
    // Sampled even when the boot centre looked implausible, so the diagnostic
    // readouts stay live while you are still fixing the wiring. Only event
    // generation is suppressed, to avoid phantom input from a floating pin.
    lastXValue = readAxis(JOYSTICK_X_PIN);
    lastYValue = readAxis(JOYSTICK_Y_PIN);

    if (!axesDetected) return JoystickEvent::None;

    const int8_t x = direction(lastXValue, centreXValue, heldX);
    const int8_t y = direction(lastYValue, centreYValue, heldY);

    // X wins when the stick is pushed diagonally: tuning is the more common
    // intent than paging, and alternating between the two feels broken. Note the
    // Y check is skipped for as long as X is deflected at all, not merely
    // between X repeats.
    const bool xFired = shouldFire(x, heldX, heldXSince, heldXLastFire, now);
    if (xFired) {
        return x > 0 ? JoystickEvent::Right : JoystickEvent::Left;
    }
    if (heldX != 0) return JoystickEvent::None;

    if (shouldFire(y, heldY, heldYSince, heldYLastFire, now)) {
        return y > 0 ? JoystickEvent::Down : JoystickEvent::Up;
    }

    return JoystickEvent::None;
}

int rawX() { return lastXValue; }
int rawY() { return lastYValue; }
int centreX() { return centreXValue; }
int centreY() { return centreYValue; }
bool buttonDown() { return buttonPressed; }
bool detected() { return axesDetected; }

const char* eventName(JoystickEvent event) {
    switch (event) {
        case JoystickEvent::Up:        return "Up";
        case JoystickEvent::Down:      return "Down";
        case JoystickEvent::Left:      return "Left";
        case JoystickEvent::Right:     return "Right";
        case JoystickEvent::Click:     return "Click";
        case JoystickEvent::LongClick: return "LongClick";
        default:                       return "-";
    }
}

}  // namespace joystick
