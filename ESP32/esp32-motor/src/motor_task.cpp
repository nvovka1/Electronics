#include "motor_task.h"

#include "board_pins.h"
#include "jog_command.h"
#include "stepper.h"

namespace {

// How long to sleep between checks while the stick is centred. Short enough
// that the motor responds immediately, long enough that an idle task is not
// burning a core.
constexpr uint32_t IdlePollMs = 10;

}  // namespace

void motorTask(void* parameter) {
    (void)parameter;

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    stepperBegin();
    Serial.println("[motor] ready, push the joystick left or right");

    JogCommand command{};
    bool coilsLive = false;

    for (;;) {
        // Peek rather than receive: the value stays in the mailbox so it can be
        // read again next time round. The joystick task replaces it when it has
        // something newer.
        xQueuePeek(jogMailbox, &command, 0);

        if (!command.moving) {
            if (coilsLive) {
                // Drop the coils as soon as the stick returns to centre. The
                // 64:1 gearbox holds the shaft without any current, so holding
                // torque would cost 240 mA and buy nothing.
                stepperRelease();
                digitalWrite(PIN_LED, LOW);
                coilsLive = false;
            }
            vTaskDelay(pdMS_TO_TICKS(IdlePollMs));
            continue;
        }

        if (!coilsLive) {
            digitalWrite(PIN_LED, HIGH);
            coilsLive = true;
        }

        // One half-step, then wait. The wait is what sets the speed, and it
        // comes straight from how far the stick is pushed.
        stepperStepOnce(command.direction);

        // delayMicroseconds rather than vTaskDelay: one FreeRTOS tick is 1 ms,
        // so vTaskDelay cannot express anything between 1 and 2 ms - which is
        // exactly where this motor's usable top speed lives.
        //
        // The cost is a busy-wait rather than a yield, holding core 1 for as
        // long as the stick is held. That is affordable here: this task is the
        // only thing on core 1 that matters, and the Arduino core does not run
        // the task watchdog against core 1's idle task.
        delayMicroseconds(command.stepIntervalUs);
    }
}
