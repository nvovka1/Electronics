#pragma once

// Samples the joystick's X axis and publishes the current jog state into
// jogMailbox: which way to turn and how fast, or stop.
void joystickTask(void* parameter);
