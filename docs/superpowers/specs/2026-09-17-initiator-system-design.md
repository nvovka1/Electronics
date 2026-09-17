# Initiator system — design

**Date:** 2026-09-17
**Status:** approved, ready for planning

Three linked projects: a fleet backend that shows linked devices and dispatches
commands, a handheld LoRa controller with three buttons, and a receiving node
that runs the Init/Arm/Fire/Safe state machine behind four LEDs and an OLED.

```
  controller ──LoRa──► explosion ──WiFi/HTTP──► Backend/Initiator ──► operator
   3 buttons          4 LEDs + OLED              .NET 9 · Mongo · MVC
   OLED               state machine
      ◄────ACK────────
```

Each project is built and specced separately. This document is the shared
design they all agree on; the per-project plans come after it.

## Scope note

The FIRE output is an LED and a line of text on a screen. This project builds a
state machine, a radio protocol and a dashboard. It does not design, drive or
describe any firing circuit, and nothing here is wired to anything but an
indicator.

## Constraint: no existing project is modified

Three directories may be written to, and no others:

```
Backend/Initiator/
ESP32/initiator/controller/
ESP32/initiator/explosion/
```

`Backend/LoraFleet` and `ESP32/lora-queue-release` are **read-only references**.
They are read for their patterns and their proven code, and nothing in them is
edited, moved or refactored — not the frame codec, not `LogDictionary`, not
`platformio.ini`. Where this design says a new project reuses something from
one of them, it means the new project gets **its own copy inside its own
folder**, which it is then free to change without the original caring.

That copying is deliberate duplication. Sharing a `lib/` between the two
firmwares, or project-referencing `LoraFleet.Domain`, would couple projects that
should be able to change independently — and would mean touching the existing
tree to do it.

---

## 1. Shared contracts

Everything in this section is depended on by more than one project. Changing
any of it means changing at least two.

### 1.1 The state machine

Four states. One transition table, written twice — C# in the backend, C++ in
the explosion node. The controller does **not** hold this table: it only needs
to know which command comes next in the sequence (`SAFE→INIT`, `INIT→ARM`,
`ARMED→FIRE`), which is a three-entry list, and it never decides whether a
command is legal. That decision belongs to the node that holds the state.

| From \ Command | `INIT` | `ARM` | `FIRE` | `SAFE` | *timer* |
|---|---|---|---|---|---|
| **SAFE**  | → INIT          | reject    | reject    | — (no-op) | — |
| **INIT**  | restarts timer  | → ARMED   | reject    | → SAFE    | **→ ARMED after 5 min** |
| **ARMED** | reject          | — (no-op) | → FIRE    | → SAFE    | — |
| **FIRE**  | reject          | reject    | — (no-op) | → SAFE    | — |

INIT is the only state with a timed exit. Entering it starts a countdown —
`AutoArmTimeoutMs`, five minutes by default — and when it expires the node moves
itself to ARMED with no command from anyone. An explicit ARM during those five
minutes still works immediately; the timer is a fallback, not a replacement.
Leaving INIT for any reason cancels the countdown.

Rules that fall out of the table and must not be re-derived anywhere:

- **Boot state is always SAFE.** It is never restored from NVS. A node that
  loses power and comes back must not come back armed or fired.
- **SAFE is the only revoke path.** From SAFE the sequence starts again at
  INIT; there is no way back into ARMED except through INIT.
- **A no-op is not a rejection.** Re-sending the command for the state you are
  already in succeeds. This matters because the radio retries: a lost ACK must
  not turn a successful command into a failed one. **INIT while in INIT is the
  one no-op that does something** — it restarts the five-minute countdown, which
  is how an operator holds a node in INIT while still setting up. It is still
  reported as accepted, and it is the only exception to "a no-op changes
  nothing".
- **ARM does not expire.** Chosen deliberately. Combined with the INIT timer
  above, this is worth stating plainly rather than leaving to be discovered: a
  node that is sent INIT and then forgotten about arms itself after five
  minutes and stays ARMED indefinitely, with nobody having pressed anything
  since. A SAFE command — from the controller or from the dashboard — is the
  only thing that clears it, and losing the controller does not.
- **The countdown does not survive a reboot.** Boot is SAFE, so a node that
  loses power during INIT comes back in SAFE with no timer running, and the
  sequence starts again.

Rejection reasons are a small enum shared with the wire protocol:
`OK`, `BAD_TRANSITION`, `BAD_TARGET`, `REPLAY`, `BAD_COMMAND`.

#### Keeping the two copies honest

`lora-queue-release`'s `LogCode` enum and LoraFleet's `LogDictionary` already
have this problem, and the answer there is generation plus a test. Same answer
here: the explosion node's table lives in one header, and a backend test parses
that header and asserts the C# table agrees with it, cell by cell. A table that
drifts fails the build rather than misbehaving in the field.

### 1.2 LoRa frame

Each new firmware carries **its own copy** of the frame codec, under its own
`lib/proto/`. `lora-queue-release` is not touched. The wire format is the same
protocol v1 that project defines — same `0xA5` sync, header, CRC16-CCITT and
SRC/SEQ handling — with two new type codes taken from the core range, so the
two families of node could share air without confusing each other.

| Type | Name | Payload |
|---|---|---|
| `0x10` | `MSG_CMD` | `dst:u16, command:u8, counter:u32` (7 bytes) |
| `0x11` | `MSG_CMD_ACK` | `counter:u32, accepted:u8, state:u8, reason:u8` (7 bytes) |
| `0x12` | `MSG_STATE` | `dst:u16, state:u8, cause:u8` (4 bytes) |

- `dst` is the target node id. A node silently ignores a command addressed
  elsewhere — it does not ACK, because an ACK would tell a controller its
  command had reached the right node.
- `command` is `1=INIT 2=ARM 3=FIRE 4=SAFE`.
- `counter` is monotonic per source, held in NVS across reboots. **A node
  rejects any command whose counter it has already seen from that source.**
  Without this, a recorded FIRE frame can simply be replayed at the node.
- The ACK carries the node's resulting state whether or not the command was
  accepted, so a controller that has lost track always resynchronises from the
  node rather than guessing.

**`MSG_STATE` is how a controller hears about a transition it did not cause** —
today that means the auto-arm, and anything similar added later. The node sends
it to whichever node last commanded it. `cause` is `1 = auto_arm`, leaving room.

It is unacknowledged, and sent `StateAnnounceRepeats` times (three) spaced
`StateAnnounceIntervalMs` apart, because a single unacknowledged frame is a
coin toss. The controller de-duplicates on the frame's `SEQ`, so hearing the
same announcement twice is harmless. Adding an ACK path for it would double the
protocol to make a fallback slightly more reliable than the real backstop
already is: **the next command's ACK carries the true state**, so a controller
that misses all three announcements is wrong for one button press and then
correct.

### 1.3 HTTP API

Device-facing, `X-Api-Key` header, same shape and reasoning as LoraFleet's
device API.

| Method | Route | Purpose |
|---|---|---|
| `POST` | `/api/v1/devices/{serial}/health-reports` | enrol on first call, heartbeat after |
| `POST` | `/api/v1/devices/{serial}/state-events` | every transition, accepted or rejected |
| `POST` | `/api/v1/devices/{serial}/log-records` | ring log, as LoraFleet |
| `GET`  | `/api/v1/devices/{serial}/commands/next` | `204` when nothing queued |
| `POST` | `/api/v1/devices/{serial}/commands/{commandId}/result` | `applied` or `rejected` + reason |

A state event is `{ timestampMs, fromState, toState, command, source, accepted,
reason }` where `source` is `lora`, `api` or `timer` — the last being the
auto-arm, which is a transition with no command behind it and so has `command`
null. `timestampMs` is the
node's own monotonic clock, as in LoraFleet; absolute time is stamped on
receipt.

**Dashboard commands reach a node by polling**, so there is up to one poll
interval of latency. Accepted: the dashboard is for setup and for the
after-the-fact record, and the controller is the real-time path.

---

## 2. Backend/Initiator

.NET 9, MongoDB, Razor MVC. Its own solution and its own database — a fleet
firmware problem must not be able to take down command dispatch. Layout mirrors
`Backend/LoraFleet` so the two read alike.

```
src/Initiator.Domain/       no dependencies
  Devices/                  Device, DeviceHealth, DeviceStatusPolicy
  States/                   NodeState, CommandType, RejectReason, NodeStateMachine
  Commands/                 Command, CommandStatus
  Logs/                     LogRecord, LogDictionary  (own copy, LoraFleet's untouched)
src/Initiator.DataAccess/   Mongo context, class maps, repositories, indexes
src/Initiator.Web/          Controllers/Api/ + MVC Controllers/ + Views/
tests/Initiator.Tests/      pure logic + EphemeralMongo mapping tests
```

`NodeStateMachine` is a pure static class over the table in §1.1 — no Mongo, no
HTTP, no clock. It is the single place the backend knows the rules.

**The backend does not run the auto-arm countdown; it only reports it.** The
timer belongs to the node, and duplicating it here would create a second clock
that can disagree with the one that matters. `NodeStateMachine` therefore stays
clock-free: it knows `INIT --timer--> ARMED` is a legal transition, so it can
accept the state event when it arrives, but it never decides that the five
minutes are up. The device page renders a countdown from the last INIT event's
timestamp and labels it an estimate. A node that has armed itself while the
dashboard still showed INIT is the normal case for a few seconds, and the arriving
state event settles it.

### Command lifecycle

`Pending → Delivered → Applied | Rejected`, plus `Expired`. A command is
`Delivered` when a node has fetched it and `Applied` when the node reports the
transition. A command still `Pending` after a configured age becomes `Expired`
rather than being delivered late to a node that has been offline for an hour —
an INIT arriving twenty minutes after it was clicked is not what the operator
asked for.

The backend refuses at queue time to enqueue a command the table rejects for the
node's last known state, so the operator gets the error immediately. **The node
is still the final authority** — the backend's view can be stale, and a node
that rejects a command reports it back as `Rejected` with a reason.

### Pages

- **Devices** — every enrolled node: serial, node id, current state as a
  coloured badge, last seen, battery, RSSI, link status.
- **Device details** — live state, a row of `Init / Arm / Fire / Safe` buttons
  with illegal transitions disabled, transition history, recent logs.
- **Commands** — queue and history, each row showing whether it came from the
  controller or the dashboard, and how it ended.
- **Logs** — decoded ring log, filtered by device, level and tag, as LoraFleet.

No user authentication on the dashboard, matching LoraFleet, and documented in
its README as the same known gap.

---

## 3. ESP32/initiator/controller

A new PlatformIO project in its own folder, patterned on `lora-queue-release`
and copying the parts worth copying — FreeRTOS task layout, frame codec, ring
log, NVS config, serial shell. The original is read, never edited. **No WiFi and
no OTA**: this node never talks to the network, so neither is copied across.

### Pins

| Function | Pin | Notes |
|---|---|---|
| `BTN_SEQ` | GPIO 0 | onboard PRG button, `INPUT_PULLUP` |
| `BTN_SAFE` | GPIO 32 | external to GND, `INPUT_PULLUP` |
| `BTN_TARGET` | GPIO 33 | external to GND, `INPUT_PULLUP` |
| LoRa, OLED | board defaults | unchanged from `board_pins.h` |

Three buttons, one job each. No long-press, no double-press, no chords: a
control that means two things is a control that eventually does the wrong one.

### Tasks

| Task | Owns | Does |
|---|---|---|
| `buttonTask` | the three GPIOs | debounce, post a `ButtonEvent` |
| `commandTask` | believed state, target id, counter | decide the command, send, retry, consume the ACK |
| `radioTask` | the SX1276 | transmit frames, receive ACKs |
| `uiTask` | the OLED | draw |

`commandTask` sends the command that the table says comes next for its
*believed* state, retries a fixed number of times when no ACK arrives, and
**updates its belief only from an ACK.** When the retries run out it shows
`LOST` and leaves its belief untouched — a controller that advances its own
display on an unacknowledged send is a controller that lies.

`BTN_SAFE` always sends SAFE, from any believed state, including `LOST`.

**A `MSG_STATE` announcement updates the belief exactly as an ACK does.** This
is the other half of the auto-arm: without it the controller would still show
INIT after the node had armed itself, and the next `BTN_SEQ` press would send a
pointless ARM instead of FIRE. Repeats are de-duplicated on the frame's `SEQ`.

OLED shows: target node id, believed state, last ACK result, RSSI. While the
believed state is INIT it also shows roughly how long is left before the node
arms itself — the controller knows `AutoArmTimeoutMs` and when it saw the INIT
ACK. **This display is an estimate, not a clock the node is obeying**, and the
spec says so because the temptation to then advance the belief locally when it
hits zero is exactly the lie the ACK rule forbids. The belief changes only when
the node says so.

---

## 4. ESP32/initiator/explosion

Same skeleton, plus its own copy of `net_task` and `fleet_client` adapted to the
API in §1.3. **OTA is not included** — `lora-queue-release` has it, this project
does not need it, and it is a whole subsystem to get wrong.

### Pins

| Function | Pin | Colour | Notes |
|---|---|---|---|
| `LED_SAFE` | GPIO 32 | blue | |
| `LED_INIT` | GPIO 33 | green | |
| `LED_ARMED` | GPIO 2 | yellow | strapping pin; LED to GND holds it low, the boot-safe direction |
| `LED_FIRE` | GPIO 14 | red | routed to the unused microSD socket, as GPIO 13 already is |

GPIO 16 is not used for anything. On these PICO-D4 boards it is the embedded
flash chip select, and driving it is what turns a board into a silent watchdog
reboot loop.

### Tasks

| Task | Owns | Does |
|---|---|---|
| `radioTask` | the SX1276 | decode frames, check `dst` and counter, post to the state queue, send the ACK |
| `netTask` | WiFi + HTTP | poll for commands, post state events and logs |
| `stateTask` | **the state** | apply the table, drive the LED and UI queues |
| `ledTask` | the four LEDs | light exactly one |
| `uiTask` | the OLED | draw |

`stateTask` is the **sole owner of the state**, and both command sources —
LoRa and the API poll — land on its one queue. They therefore serialise: if two
commands arrive together the second is evaluated against the state the first
produced, not against the state both of them saw. This is the whole reason the
state is not a shared variable.

**The auto-arm countdown needs no timer object.** `stateTask` blocks on its
queue with a timeout: `portMAX_DELAY` in every state but INIT, and the
milliseconds remaining until the deadline while in INIT. A command wakes it
early; nothing arriving means the receive times out, which *is* the countdown
expiring. Entering INIT sets the deadline, re-entering it resets the deadline,
leaving INIT clears it. One task, one variable, no software timers and no second
thing that can be running when the state says it should not be.

On the auto-arm the task does exactly what it does for a commanded transition —
drives the LEDs and the OLED, queues a state event for the backend — and
additionally asks `radioTask` to announce it with `MSG_STATE`.

`ledTask` lights exactly one LED, always the current state. There is no
combination of LEDs and no off state while running.

OLED shows: state in large text, the last command with its source and whether
it was accepted, WiFi and LoRa link status.

### Offline is not an error

The node never blocks on the network. State transitions are applied and shown
on the LEDs whether or not WiFi is up; the events buffer in the ring log and
flush when the connection returns. A node out of coverage is a node that still
works, with a gap in its record.

---

## 5. Code style these firmwares must hold to

Written here because "keep it simple" is not reviewable but these are.

- **No magic numbers.** Every pin, timeout, retry count, queue depth, stack
  size, interval and buffer length is a named `constexpr` in one `config.h` per
  firmware. A literal number in a `.cpp` is a review comment. `AutoArmTimeoutMs`
  is one of these — five minutes is its default, not a number written anywhere
  in the state machine — and it is settable per node over the serial shell and
  stored in NVS, because five minutes is a guess about how long setting up takes
  and that is a field question, not a build question.
- **One task, one responsibility, one owner per peripheral.** Tasks communicate
  only through FreeRTOS queues — no shared mutable globals, and so no mutexes,
  because nothing is shared.
- **No dynamic allocation after `setup()`.** Fixed-size buffers, statically
  allocated queues.
- **The transition table is a table** — a literal array walked by a lookup, not
  a nest of `if`s. It should read against §1.1 cell by cell.
- **Plain C-style structs** for messages. No templates, no inheritance, no
  virtual dispatch in the firmware.

---

## 6. Testing

| Where | What |
|---|---|
| `pio test -e native` | the transition table against §1.1, frame encode/decode round-trip for the three new types, replay rejection, malformed and mis-addressed frames |
| `pio test -e native` — auto-arm | the countdown fires from INIT and only from INIT; SAFE and an explicit ARM both cancel it; a second INIT restarts it; the deadline is cleared on leaving INIT, so a node that goes INIT → SAFE → INIT does not arm early |
| `dotnet test` — pure | `NodeStateMachine` against §1.1, command lifecycle and expiry, log dictionary |
| `dotnet test` — Mongo | EphemeralMongo, as LoraFleet: BSON mapping, and that a device check-in never overwrites operator-set fields |
| `dotnet test` — contract | parses `ESP32/initiator/explosion`'s transition-table header and asserts the C# table matches. Read-only, and the only cross-project file access in the build |
| On hardware | the sequence end to end, a rejected FIRE from SAFE, a SAFE from every state, a reboot from ARMED coming back SAFE, a node surviving the backend being unreachable, and — with the timeout turned down so the test takes seconds — a node arming itself while the controller watches, with the controller's OLED following it |

## 7. Build order

1. **Backend/Initiator** — nothing depends on it being finished, and both
   firmwares need something to talk to.
2. **ESP32/initiator/explosion** — the state machine and the API client. Can be
   driven entirely from the dashboard before a controller exists.
3. **ESP32/initiator/controller** — the radio path, tested against a node whose
   behaviour is already known good.

## 8. Decisions taken during design

Standalone backend solution; Razor MVC rather than Angular; both boards LILYGO
LoRa32 v2.1; LoRa ACK plus WiFi polling, with no WiFi on the controller; FIRE
latched until SAFE; ARM without a timeout; **INIT auto-arms after five minutes**,
announced to the controller with `MSG_STATE` and restarted by a second INIT;
targeted addressing; three buttons on the controller, one job each.

No open items.
