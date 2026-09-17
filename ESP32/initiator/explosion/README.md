# initiator / explosion

The node that holds the state. It receives Init/Arm/Fire/Safe from the handheld
controller over LoRa and from the backend's queue over WiFi, runs the state
machine, shows the result on four LEDs and an OLED, and reports every transition
to [`Backend/Initiator`](../../../Backend/Initiator).

PlatformIO · ESP32 Arduino · LILYGO LoRa32 v2.1.

Design: [`docs/superpowers/specs/2026-09-17-initiator-system-design.md`](../../../docs/superpowers/specs/2026-09-17-initiator-system-design.md).
Wiring and the antenna warning: [WIRING.md](WIRING.md).

> **This node's FIRE output is a red LED and a line on a screen.** It is a state
> machine and an indicator. There is no firing circuit here and nothing is
> driven but LEDs.

---

## Quick start

```bash
# host unit tests, no board needed
~/.platformio/penv/Scripts/pio.exe test -e native                      # needs gcc
powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1  # uses MSVC

# build and flash
~/.platformio/penv/Scripts/pio.exe run -e dev -t upload --upload-port COM5

# give the board its identity
~/.platformio/penv/Scripts/pio.exe device monitor --port COM5
> set node_id 2
> status
```

Two images, one codebase:

| Env | For | Logging | `cmd` over the cable |
|---|---|---|---|
| `dev` | the desk | everything, to UART | yes |
| `field` | left somewhere | INFO and above, not to UART | **no** |

DEBUG and TRACE are not switched off in `field`, they are *not compiled* — so
they cannot switch themselves back on. INFO stays, because that is where the
state transitions live and those are the whole point of this node.

---

## The state machine

| From \ Command | `INIT` | `ARM` | `FIRE` | `SAFE` | *timer* |
|---|---|---|---|---|---|
| **SAFE**  | → INIT          | reject    | reject    | — | — |
| **INIT**  | restarts timer  | → ARMED   | reject    | → SAFE | **→ ARMED after 5 min** |
| **ARMED** | reject          | —         | → FIRE    | → SAFE | — |
| **FIRE**  | reject          | reject    | —         | → SAFE | — |

- **Boot is always SAFE**, never restored from NVS. A node that loses power
  mid-sequence must not come back armed or fired.
- **SAFE is accepted from every state**, including FIRE, which is otherwise
  latched.
- **Re-sending the current state's command succeeds** and changes nothing. The
  radio retries, so a lost ACK must not turn a success into a failure. INIT
  while in INIT is the one exception: it restarts the countdown, which is how
  you hold a node in INIT while still setting up.
- **ARM does not expire.** Together with the INIT timer that means a node sent
  INIT and then forgotten arms itself after five minutes and stays ARMED. Only
  a SAFE clears it, and losing the controller does not.

`set autoarm <seconds>` changes the timeout per node; five minutes is only the
default.

The same table is in the backend in C#, and a test there parses
[`lib/state/node_state.cpp`](lib/state/node_state.cpp) and fails the build if
the two disagree. If you reshape the table, reshape that parser with it — do not
delete the test.

---

## How it is put together

Five tasks, one responsibility each, talking **only** through queues. Nothing is
shared between them, which is why there is not a mutex anywhere in this
firmware.

| Task | Owns | Does |
|---|---|---|
| `radio` | the SX1276 | decode, check address and counter, ACK, announce |
| `state` | **the state** | apply the table, run the countdown |
| `led` | the four LEDs | light exactly one |
| `ui` | the OLED | draw |
| `net` | WiFi and HTTP | report, poll, drain the buffers |

Plus a `shell` task, so a board can be commissioned over the cable.

### The state task owns the state

Both command sources — the radio and the backend poll — land on **one queue**.
They therefore serialise: if two commands arrive together the second is
evaluated against the state the first produced, not against the state both of
them saw. That is the whole reason the state is not a variable other tasks can
reach.

### The countdown is a queue timeout

There is no timer object. `stateTask` blocks on its queue with a timeout —
`portMAX_DELAY` in every state but INIT, and the time remaining while in INIT. A
command wakes it early; nothing arriving *is* the countdown expiring. One task,
one deadline variable, and nothing that can still be running when the state says
it should not be.

### Offline is not an error

The node applies commands, drives its LEDs and draws its screen whether or not
WiFi is up. Transitions buffer in RAM and flush when the connection returns; the
oldest are dropped first if the buffer fills, because the end of an outage
matters more than its start. A node out of coverage is a node that still works,
with a gap in its record.

The backend is on Render's free plan, which sleeps after about fifteen minutes
idle and takes tens of seconds to wake. The HTTP timeout is 30 s for that
reason: a short one turns a sleeping service into a node that reports itself
broken.

### Replay protection

A command frame carries a counter, monotonic per controller, kept in NVS across
reboots. The node refuses a counter it has already seen.

The CRC proves a frame is intact, not that it is new — anyone who records one
FIRE frame off the air can transmit the same bytes again, and every other check
would pass, because it really did come from the controller once. The counters
are reloaded from NVS on the first frame from each controller after a boot,
without which power-cycling the node would be the way around the check.

A repeated counter is **answered, not ignored**: the commonest cause is a
controller retransmitting because our ACK was lost, and silence would make it
retry until it reported LOST for a command the node had already carried out.

### Telling the controller about the auto-arm

When the node arms itself it sends `MSG_STATE` to whoever last commanded it,
three times, unacknowledged. Without it the controller would still be showing
INIT after the node had armed, and the operator's next press would send a
pointless ARM instead of FIRE.

There is no ACK path for the announcement, deliberately: it would double the
protocol to make a fallback more reliable than the real backstop, which is that
the next command's ACK carries the true state. A controller that misses all
three is wrong for one button press and then correct.

---

## Commissioning

The image ships knowing the WiFi network, the service URL and the API key (see
[fleet.ini](fleet.ini)), so a freshly flashed board is already commissioned. All
that is left is its identity:

```
> set node_id 2
```

Everything else is only needed to move a node somewhere new:

```
> set wifi <ssid> <pass>
> set url https://initiator-70pm.onrender.com
> set key <api-key>        # stored in NVS, beats the compiled-in value
> set autoarm 300
> reset                    # back to what the image was built with
```

`status` and `net` show what the node thinks. Neither ever prints the API key —
only whether there is one and how long it is, which is all you need to tell a
missing key from a wrong one.

The node enrols itself on its first report; there is no serial number to type in
anywhere. A board that has never been provisioned derives its serial from the
chip MAC, so there is never a node without one.

---

## Serial commands

| Command | |
|---|---|
| `status` | state, countdown, links, buffer depths |
| `net` | WiFi and service settings |
| `set node_id <n>` | this node's protocol address |
| `set autoarm <seconds>` | INIT → ARMED timeout |
| `set wifi <ssid> <pass>` | |
| `set url <base-url>` | |
| `set key <api-key>` | |
| `reset` | settings back to the built-in defaults |
| `cmd <init\|arm\|fire\|safe>` | **`dev` builds only** — drive the state machine from the cable |

`cmd` is not compiled into a `field` image. A way to command a node that
bypasses both the radio and the backend belongs on a bench.

---

## Tests

```bash
powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1
```

24 tests over everything in `lib/`, in about a second, with no board:

- the transition table, cell by cell against the spec, plus the rules that fall
  out of it — boot is SAFE, SAFE is accepted everywhere, FIRE is only reachable
  from ARMED;
- the auto-arm: it fires from INIT and from nowhere else. If that ever passes
  for SAFE, a node sitting idle can arm itself with nobody having touched it;
- the frame codec: round trips, a flipped bit failing the CRC, a frame that lies
  about its length, a foreign sync byte, a version this build does not speak;
- the replay guard: a repeat refused, counters tracked per controller, a zero
  counter refused, and the table surviving more controllers than it has slots.

`scripts/run_native_tests.ps1` exists because `pio test -e native` needs gcc and
this machine has MSVC. Both build the identical sources, so a green run either
way means the same thing.

The tests avoid designated initializers (`{.dst = 2}`) on purpose: they are a
GCC extension in C++17 and MSVC refuses them, so the file has to be written
without them to build under both.

---

## What this node deliberately does not have

**OTA.** `ESP32/lora-queue-release` has a working over-the-air updater. This
project does not need one and has not copied it: it is a whole subsystem, and an
update path that can go wrong is worse than a cable on a node that lives on a
bench.

**A firing circuit.** See the note at the top.
