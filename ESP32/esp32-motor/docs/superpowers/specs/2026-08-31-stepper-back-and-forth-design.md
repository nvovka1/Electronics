# Back-and-forth stepper control — design

**Date:** 2026-08-31
**Project:** `ESP32/esp32-motor`

## Purpose

One button drives a 28BYJ-48 between two positions a quarter turn apart. Press
once, the shaft turns 90° to position B. Press again, it returns to A.

## Hardware

| Component | Role |
|---|---|
| ESP32 (`esp32dev`, WROOM-32) | Controller, USB-powered |
| ULN2003 board | Eight Darlington transistors. No logic: four inputs, one per coil. |
| 28BYJ-48 | 5 V unipolar, geared 64:1 → 4096 half-steps per output revolution |
| Tactile button | Internal pull-up, active LOW |

Pins: IN1 18, IN2 19, IN3 21, IN4 22, BUTTON 32, LED 2. Full wiring in
[`WIRING.md`](../../../WIRING.md).

### Why this replaced the original A4988 + 17HS4023 build

The bipolar build was abandoned after bring-up stalled: 12 V never reached the
driver's `VMOT` through the relay, and the fault was not isolated. The 28BYJ-48
removes every failure mode encountered — a keyed connector instead of coil
pairing, no current limit to set, no `RESET`/`SLEEP` jumper, no relay, no 12 V
rail, and four board LEDs that make stepping visible without a meter.

## Behaviour

- **Travel:** 1024 half-steps = 90° per press. Single tunable constant.
- **Direction:** alternates every completed move. Start position is A.
- **Mid-move presses are ignored** — the position is always exactly A or B.
- **Idle:** all four coils dropped. No current, and a 64:1 gearbox does not
  back-drive, so the shaft holds position without holding torque.

## Architecture

```
button_task (core 0)          motor_task (core 1)           stepper
     │                              │                          │
     │  MoveCommand ──> queue ──>   │                          │
     │                              │  stepperMove(n, dir) ──> │  (blocks)
```

| Unit | Responsibility | Interface |
|---|---|---|
| `button_task` | Debounce, detect the press edge | pushes `MoveCommand` into `moveQueue` |
| `motor_task` | Owns position state, runs one move per command | pops the queue, calls `stepperMove` |
| `stepper` | Owns the coil sequence and the stepping loop | `stepperMove(steps, direction)` |

### Stepping

The ULN2003 has no step/direction logic — it is four switches — so the firmware
produces the coil pattern itself: eight half-step patterns alternating one
energised coil with two adjacent ones. Direction is which way the index walks
the ring. The phase index persists across moves, because restarting from phase 0
each time would jerk the rotor back to wherever phase 0 sits.

`stepperMove` is a plain blocking loop: advance the phase, write four pins,
`vTaskDelay` 2 ms, repeat; drop the coils at the end.

**This deliberately replaced a hardware-timer ISR design.** The original build
drove an A4988, which needs STEP pulses measured in microseconds — far below
what `vTaskDelay` can express, so a timer ISR with tick-counting and a
semaphore was warranted. The 28BYJ-48 steps every 2 ms, which the scheduler
handles directly. Keeping the ISR would have been carrying the cost of that
machinery — volatile state, IRAM constraints, an interrupt-to-task handoff —
to solve a problem this motor does not have, while obscuring the coil sequence
that is the actual subject of the code.

There is also **no acceleration ramp**. Steppers normally need easing up to
speed, but the 64:1 gearbox leaves a large torque margin at 2 ms per step and
the motor starts from rest without one. A faster or ungeared motor would need
it back.

## Error handling

- `stepperMove` always drops the coils before returning, including on the path
  where the caller does nothing further, so the motor is never left energised.
- Being a blocking call with no failure mode, a move cannot half-happen: control
  returns only once every step has been issued, so the position flip that
  follows it is always truthful.

## Testing

1. **Quarter turn.** Mark the shaft; confirm 90° per press and a return to the
   mark.
2. **Mid-move press.** Press repeatedly during travel; the shaft must land on
   the mark, not between positions.
3. **Idle current.** Confirm the driver LEDs go dark between moves and the chip
   stays cool.
4. **Brownout.** Confirm the ESP32 does not reset when the motor starts. If it
   does, the ULN2003 needs its own 5 V supply with a common ground.

The diagnostic build environments used during bring-up (`bench`, `bench-slow`,
`dry-run`, `relay-test`) have been removed now that the hardware is working.

## Out of scope

Open loop — no homing, no limit switches, no feedback. If the shaft is forced or
stalls, the firmware's idea of position drifts from reality. Accepted for a
one-button build.

## The one number to tune

`StepsPerMove = StepsPerRevolution / 4` in `motor_task.cpp`.
