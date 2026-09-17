# initiator / controller

The handheld. Three buttons, an OLED, and a radio. It sends Init/Arm/Fire/Safe
to an [explosion node](../explosion) over LoRa and shows what the node said
back.

PlatformIO · ESP32 Arduino · LILYGO LoRa32 v2.1.

Design: [`docs/superpowers/specs/2026-09-17-initiator-system-design.md`](../../../docs/superpowers/specs/2026-09-17-initiator-system-design.md).
Wiring and the antenna warning: [WIRING.md](WIRING.md).

---

## Quick start

```bash
# host unit tests, no board needed
~/.platformio/penv/Scripts/pio.exe test -e native                      # needs gcc
powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1  # uses MSVC

# build and flash
~/.platformio/penv/Scripts/pio.exe run -e dev -t upload --upload-port COM6

# give it its identity and tell it which node to aim at
~/.platformio/penv/Scripts/pio.exe device monitor --port COM6
> set node_id 100
> set target 2
> status
```

Controllers are numbered from 100 by default so a controller id is never
mistaken for a node id at a glance.

---

## The three buttons

| Button | Pin | Does |
|---|---|---|
| **SEQ** | GPIO 0 (on-board PRG) | sends the next command in the sequence |
| **SAFE** | GPIO 32 | sends SAFE. From any state. Always. |
| **TARGET** | GPIO 33 | cycles which node is being commanded |

One job each. No long-press, no double-press, no chords: a control that means
two things is a control that eventually does the wrong one.

The sequence is three entries long:

```
SAFE  -> INIT
INIT  -> ARM
ARMED -> FIRE
FIRE  -> nothing; press SAFE
```

---

## What this board deliberately does not know

**It does not hold the transition table.** The node holds the state, so the node
decides whether a command is legal. This board only knows which command comes
next, which is the three-entry list above. Keeping it to that is what stops the
controller from being a second opinion that can disagree with the node.

**It has no WiFi and never talks to the backend.** There is no `fleet.ini` here
and no API key to keep out of git. Everything the dashboard knows about what
this controller did, it learned from the node.

**It does not remember what it believed.** The belief is never written to NVS. A
controller that comes back after a power cut believing something about a node it
has not spoken to since is worse than one that admits it does not know — so on
boot it shows `?` and the first command's ACK fills it in.

---

## Believing only what comes back

The belief on the screen changes when the node says so, and at no other time.

> A controller that advances its own display on an unacknowledged send is a
> controller that lies — and the operator's next press is then aimed at a state
> the node is not in.

Each command is sent up to three times, 700 ms apart. What can happen:

| Outcome | Screen | Belief |
|---|---|---|
| ACK, accepted | `ARM ok` | moves to whatever the ACK reported |
| ACK, refused | `FIRE REFUSED` + why | moves to whatever the ACK reported |
| no ACK after three tries | `LOST` | **unchanged**, and marked stale |

A refusal is as useful as an acceptance, because the ACK carries the node's
state either way. That is how a controller that has drifted corrects itself
rather than just reporting a failure.

After a `LOST`, the next SEQ press sends **INIT**, not the command it was
trying. The command may well have arrived and only the ACK been lost, so the
node could be in either state — and carrying on from a state we are not sure of
is how a FIRE gets sent to a node someone believes is armed and isn't. INIT is
refused from nowhere except FIRE, and its ACK reports where the node actually
is. One wasted press, then correct.

### When the node arms itself

The node's INIT state has a five-minute countdown, after which it arms itself
with nobody having pressed anything. It announces that with `MSG_STATE`, three
times, and this board treats an announcement exactly as it treats an ACK. The
screen then says `ARMED` and `node armed itself`.

Without that, the controller would still be showing INIT and the next SEQ press
would send a pointless ARM instead of FIRE.

If all three announcements are missed, the next command's ACK corrects it — so
the worst case is one button press aimed at a stale state.

### SAFE interrupts a retry

Pressing SAFE while another command is retrying abandons the retries
immediately. Waiting about two seconds for a failing FIRE to run out of attempts
before SAFE is even transmitted is the wrong way round for the one control that
is supposed to always work.

---

## Replay counters

Every command carries a counter, and the node refuses one it has already seen —
that is what stops a recorded FIRE frame from being transmitted again.

The counter must therefore **never go backwards**. Writing NVS on every button
press would wear the flash, so it is reserved in blocks of 100: a block is
claimed at boot and handed out from RAM. A power cut costs the rest of the
block, which is the right way round — skipping numbers is harmless, repeating
one makes the node refuse the command.

`reset` deliberately does **not** clear the counter. It is not a setting, it is
a promise to every node that this controller will never reuse a number.

---

## Serial commands

| Command | |
|---|---|
| `status` | target, belief, next command, counter, radio |
| `set node_id <n>` | this controller's own address |
| `set target <n>` | the node to command |
| `set targets <n>` | highest node id the TARGET button cycles to |
| `reset` | settings back to defaults — **not** the counter |

There is no `cmd` here, in either build. This board *is* the way to send a
command; a second one over the cable would only be a way to send one without
the buttons agreeing.

---

## Tests

```bash
powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1
```

11 tests over `lib/`, no board:

- the sequence advances, and **nothing follows FIRE**;
- an unknown node and a stale belief both start at INIT;
- the sequence never proposes SAFE (that is a button, not a step) and never
  proposes an invalid command;
- the frame codec round-trips a command, an ACK and an announcement, and a
  corrupted ACK fails the CRC rather than being believed.

`scripts/run_native_tests.ps1` exists because `pio test -e native` needs gcc and
this machine has MSVC. Both build the identical sources.

---

## Layout

```
lib/          pure logic, no Arduino - built for the host and tested there
  seq/        the sequence, the wire enums, the names
  proto/      the frame codec and CRC (own copy; see below)
src/          everything that touches hardware, built only for the ESP32
  config.h    every tunable number
  board_pins.h
  button_task / command_task / radio_task / ui_task / shell
test/
  test_native/
```

`lib/proto/` is a **copy** of the node's, not a shared library. The two projects
are allowed to diverge, and each has its own tests over its own copy — if the
wire format ever drifts between them, it shows up as a failing test in one of
them rather than as two nodes that cannot talk.

PlatformIO does not build `src/` for the `native` environment, which is why the
`lib`/`src` split is where it is: anything that needs to be tested on the host
has to live in `lib/`.
