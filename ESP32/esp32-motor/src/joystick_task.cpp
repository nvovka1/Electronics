#include "joystick_task.h"

#include "board_pins.h"
#include "jog_command.h"

namespace {

constexpr uint32_t SampleIntervalMs = 20;

// The ESP32's ADC is 12-bit, so a reading runs 0..4095 and the centre is
// nominally 2048. Nominally: real sticks rest anywhere near the middle, which
// is why the centre is measured at startup rather than assumed.
constexpr int AdcMax = 4095;

// How far the stick must move before anything happens. A joystick at rest
// wanders by a few tens of counts, and without this the motor would creep
// whenever it was left alone.
constexpr int DeadZone = 400;

// Speed at the edge of the dead zone, and at full deflection. The motor runs
// out of torque near 1000 us and then hums without turning, so the fast end
// keeps a margin above that.
constexpr uint32_t SlowestUs = 6000;
constexpr uint32_t FastestUs = 1200;

// Set true if pushing the stick one way turns the motor the way you did not
// expect. Nothing else needs to change.
constexpr bool InvertX = false;

// Averaging several reads costs nothing at 20 ms and removes most of the ADC's
// sample-to-sample noise, which would otherwise show up as speed flutter.
int readAxis() {
    constexpr int SampleCount = 8;
    int total = 0;
    for (int sample = 0; sample < SampleCount; ++sample) {
        total += analogRead(PIN_JOYSTICK_X);
    }
    return total / SampleCount;
}

// Maps how far the stick is pushed onto a step interval. Deflection just past
// the dead zone gives SlowestUs; full deflection gives FastestUs; in between is
// linear.
//
// Note the mapping is inverted in feel: further push means a *shorter* interval,
// because the interval is the wait between steps.
uint32_t intervalForDeflection(int deflection, int travel) {
    const int beyondDeadZone = deflection - DeadZone;
    if (travel <= 0 || beyondDeadZone <= 0) {
        return SlowestUs;
    }

    const int clamped = min(beyondDeadZone, travel);
    const uint32_t range = SlowestUs - FastestUs;
    return SlowestUs - (range * static_cast<uint32_t>(clamped)) / static_cast<uint32_t>(travel);
}

}  // namespace

void joystickTask(void* parameter) {
    (void)parameter;

    // Whatever the stick reads at startup is treated as centre. If the motor
    // creeps on its own, the stick was not centred at boot - reset the board
    // without touching it.
    //
    // The spread over half a second at rest is reported as well, because that
    // is exactly what DeadZone has to cover: how far this particular stick and
    // this particular ADC wander when nobody is touching anything.
    int restingLow = AdcMax;
    int restingHigh = 0;
    long total = 0;
    constexpr int CalibrationSamples = 25;

    for (int sample = 0; sample < CalibrationSamples; ++sample) {
        const int reading = readAxis();
        restingLow = min(restingLow, reading);
        restingHigh = max(restingHigh, reading);
        total += reading;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    const int centre = static_cast<int>(total / CalibrationSamples);
    const int wander = max(restingHigh - centre, centre - restingLow);

    Serial.printf("[joystick] centre %d of %d, resting %d..%d (wander +/-%d)\n", centre, AdcMax,
                  restingLow, restingHigh, wander);
    Serial.printf("[joystick] DeadZone is %d; roughly %d would be enough here\n", DeadZone,
                  max(wander * 4, 25));

    // Usable travel either side of centre, minus the dead zone. Taking the
    // smaller side keeps the speed mapping symmetrical even when the measured
    // centre is off-middle.
    const int travel = min(centre, AdcMax - centre) - DeadZone;
    if (travel <= 0) {
        Serial.println("[joystick] centre is too close to an end of travel.");
        Serial.println("           Check the wiring: X to GPIO 34, VCC to 3V3, GND to GND.");
    }

    bool wasMoving = false;

    for (;;) {
        const int reading = readAxis();
        const int offset = reading - centre;
        const int deflection = abs(offset);

        JogCommand command{};

        if (deflection <= DeadZone || travel <= 0) {
            command.moving = false;
        } else {
            command.moving = true;
            // Which side of centre the stick is on decides the direction; how
            // far from centre decides the speed. That is the whole control.
            const bool pushedPositive = (offset > 0);
            command.direction = (pushedPositive != InvertX) ? MoveDirection::Forward
                                                            : MoveDirection::Backward;
            command.stepIntervalUs = intervalForDeflection(deflection, travel);
        }

        // Overwrite rather than send: this is the current state of the stick,
        // and a reading from 20 ms ago is of no use to anybody.
        xQueueOverwrite(jogMailbox, &command);

        // Log only on the transitions, so holding the stick does not flood the
        // monitor.
        if (command.moving != wasMoving) {
            wasMoving = command.moving;
            if (command.moving) {
                Serial.printf("[joystick] jog %s at %u us/step\n",
                              command.direction == MoveDirection::Forward ? "forward" : "backward",
                              command.stepIntervalUs);
            } else {
                Serial.println("[joystick] centred, stopped");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SampleIntervalMs));
    }
}
