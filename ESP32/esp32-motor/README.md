# esp32-motor

A joystick's X axis drives a 28BYJ-48 stepper through a ULN2003 driver board.
Push left or right and the motor turns that way; the further it is pushed, the
faster it turns.

Wire it up with **[WIRING.md](WIRING.md)** — the motor plug is keyed so it
cannot go in wrong, but **the joystick must be powered from `3V3`, not `5V`**.

## Behaviour

| Stick | Motor |
|---|---|
| Centred | Stopped, coils off, drawing nothing |
| Pushed slightly | Turns slowly |
| Pushed fully | Turns at full speed |
| Which side | Which direction |

There are no fixed positions: the motor turns for exactly as long as you hold
the stick. The coils switch off the instant it returns to centre.

## Build

```
pio run -t upload -t monitor
```

Close the serial monitor before uploading, or use `-t upload -t monitor`
together. PlatformIO cannot open the port while a monitor holds it.

## How the motor actually turns

Inside the 28BYJ-48 are four coils spaced around a toothed iron rotor. Send
current through a coil and it becomes an electromagnet, pulling the nearest
rotor tooth towards itself. Energise the coils one after another around the
circle and the rotor keeps chasing a magnetic field that is always just ahead of
it. **That chase is the rotation.**

Nothing decides anything on the far side of the four wires. The motor contains
no electronics, and the ULN2003 is just four switches — each input pin turns one
coil on or off. So the table in `stepper.cpp` *is* the motor control: walk it
one way and the shaft turns clockwise, walk it the other way and it reverses,
stop walking and it stops.

```
 phase   IN1 IN2 IN3 IN4    where the rotor is pulled
   0      ##   .   .   .    onto coil 1
   1      ##  ##   .   .    midway between coils 1 and 2
   2       .  ##   .   .    onto coil 2
   3       .  ##  ##   .    midway between coils 2 and 3
   4       .   .  ##   .    onto coil 3
   5       .   .  ##  ##    midway between coils 3 and 4
   6       .   .   .  ##    onto coil 4
   7      ##   .   .  ##    midway between coils 4 and 1
```

The two-coils-on rows are what makes this **half**-stepping: pulling on two
neighbours at once parks the rotor between them, doubling the number of
positions the motor can hold and making the motion smoother than stepping
straight from coil to coil.

After phase 7 the sequence wraps to phase 0 and the rotor has advanced one
tooth. The motor task is that idea plus the joystick:

```
loop forever:
    read the latest stick position from the mailbox
    if centred -> switch the coils off and wait
    otherwise -> advance one phase, write the four pins,
                 wait however long the stick says
```

## Speed control

How far the stick is pushed maps onto how long each phase is held. Longer wait
means slower and stronger; shorter means faster and weaker.

| | Value |
|---|---|
| Half-steps per output revolution | 4096 |
| Step interval, just past the dead zone | 6000 µs |
| Step interval, full deflection | 1200 µs |
| Full revolution | 24.6 s slowest, 4.9 s fastest |

The 28BYJ-48 is geared **64:1**, which is why the counts are large and the
motion is slow. The real ratio is 63.684:1, so a revolution is nearer 4076 steps
than 4096 — that 0.5% only matters if you count many revolutions in a row.

**Around 1000 µs is a hard wall.** Not a software limit: below it the rotor
cannot keep up with the field, so the motor stops turning and sits there humming
and drawing current. If that happens, raise `FastestUs`.

## Structure

| File | Job |
|---|---|
| `main.cpp` | Creates the mailbox, starts the two tasks |
| `joystick_task.cpp` | Reads the X axis, publishes direction and speed |
| `motor_task.cpp` | Steps for as long as the mailbox says to |
| `stepper.cpp` | The coil sequence and one-step-at-a-time output |
| `board_pins.h` | Every pin number, in one place |

`stepper` knows nothing about joysticks. `motor_task` knows nothing about coil
patterns or ADC readings. `joystick_task` knows nothing about motors.

### Why a mailbox and not a queue

A button press is an **event**: it happens once and must not be lost, so a queue
is right — nothing may be dropped.

Stick position is **state**: only the current value means anything, and a
reading from 20 ms ago is worse than useless. So `jogMailbox` is a one-slot
queue written with `xQueueOverwrite` and read with `xQueuePeek`. Each new sample
replaces the previous one instead of queueing behind it, and the motor task
always acts on the freshest value.

A normal queue here would build a backlog of stale positions, and the motor
would lag further and further behind the stick the longer you held it.

## Tuning

| Constant | Where | Meaning |
|---|---|---|
| `FastestUs` | `joystick_task.cpp` | Speed at full deflection. Lower is faster; below ~1000 it stalls |
| `SlowestUs` | `joystick_task.cpp` | Speed just past the dead zone |
| `DeadZone` | `joystick_task.cpp` | How far the stick must move before anything happens |
| `InvertX` | `joystick_task.cpp` | Set true if left and right come out swapped |

## Why there is no acceleration ramp

Steppers usually need easing up to speed or they stall on the first step. This
one does not: the 64:1 gearbox leaves a large torque margin and it starts from
rest without complaint. A ramp would be real added complexity buying nothing
here — though it would be needed again on a faster or ungeared motor.

## Known limits

Open loop, and positionless: the firmware does not track where the shaft is at
all, it only turns while the stick is held. Nothing can drift, but there is also
nothing to return to.
