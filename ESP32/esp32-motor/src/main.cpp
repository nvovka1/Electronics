// A joystick's X axis drives a 28BYJ-48 through a ULN2003 board. Push left or
// right to turn that way; the further it is pushed, the faster it turns.
// Everything runs from USB 5 V - except the joystick, which takes 3V3.
// See WIRING.md before connecting anything.

#include <Arduino.h>

#include "jog_command.h"
#include "joystick_task.h"
#include "motor_task.h"

QueueHandle_t jogMailbox = nullptr;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[main] joystick motor jog");

    // Length 1, because this is a mailbox rather than a queue: the joystick
    // task overwrites it with the current stick position, and only the latest
    // value has any meaning.
    jogMailbox = xQueueCreate(1, sizeof(JogCommand));
    configASSERT(jogMailbox != nullptr);

    // Seed it so the motor task has something valid to read before the first
    // joystick sample arrives.
    const JogCommand stopped{};
    xQueueOverwrite(jogMailbox, &stopped);

    // Joystick on core 0, motor on core 1. The motor task busy-waits between
    // steps while jogging, so it wants a core to itself.
    xTaskCreatePinnedToCore(joystickTask, "joystick", 2560, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(motorTask, "motor", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
    // All the work happens in the two tasks above. The Arduino core requires
    // this function to exist and calls it in a loop of its own, so it sleeps
    // rather than spinning and starving that core's idle task.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
