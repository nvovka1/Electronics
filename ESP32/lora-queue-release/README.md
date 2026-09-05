# lora-queue-release

A two-node LoRa Morse telegraph on LILYGO/TTGO LoRa32 v2.1 boards, built as a
demonstration of what turns firmware that works on a desk into firmware that can
be sent somewhere and left there.

Tap the key once for a dot, twice for a dash. The symbol goes into a queue, out
over LoRa in a framed packet, gets acknowledged, and appears on the other
board's screen. That part is the toy. The rest of this repository is the part
that matters: the node can name itself, log itself, test itself, be reconfigured
over a serial line, and survive having its power pulled mid-write.

---

## Quick start

```bash
# host unit tests, no board needed
~/.platformio/penv/Scripts/pio.exe test -e native      # needs gcc
powershell -ExecutionPolicy Bypass -File scripts/run_native_tests.ps1   # uses MSVC

# build all three images
~/.platformio/penv/Scripts/pio.exe run -e dev -e factory -e field

# flash two boards
~/.platformio/penv/Scripts/pio.exe run -e field -t upload --upload-port COM5
~/.platformio/penv/Scripts/pio.exe run -e field -t upload --upload-port COM6

# give the second board its own id
~/.platformio/penv/Scripts/pio.exe device monitor --port COM6
> config set node_id 2
```

Wiring, pinout and the antenna warning: [WIRING.md](WIRING.md).

---

## Three images from one codebase

The node identity is **not** a build flag — it lives in NVS, so one image serves
any board. What differs between images is what is compiled in.

| | `dev` | `factory` | `field` |
|---|---|---|---|
| Log compiled up to | TRACE (5) | DEBUG (4) | WARN (2) |
| Log echoed to UART | yes | yes | no |
| `config seed_v1`, `crash` | yes | yes | **absent** |
| `serial set`, `calib vbat` | no | yes | no |
| Lives | on the desk | on the conveyor | a year in the field |

`DEBUG` and `TRACE` in the field image are not switched off by a runtime flag —
they are not in the binary, so they can never switch themselves back on.

Test the image that ships. A dev build that is "almost the same" is how people
get surprised.

---

## Commands

Type them into the serial monitor at 115200.

| Command | What it does |
|---|---|
| `version` | build, serial, node id, uptime, reboots, POST mask, config version |
| `self-test` | re-runs the POST and prints the mask with each block's reaction |
| `health` | the 12 bytes that go out as telemetry |
| `health send` | transmit one now instead of waiting for `health_period_s` |
| `radio` | link settings and TX/RX/no-ack/bad-frame/duplicate counters |
| `frames` | last frame sent and received, in hex, decoded |
| `log dump [n]` | newest *n* records from the ring |
| `log level [0..5]` | runtime threshold, persisted through the config |
| `log clear` | empty the ring |
| `config get` | every setting with its bounds and whether it applies live |
| `config set <k> <v>` | validated, then saved, then applied — in that order |
| `config reset` | firmware defaults; keeps node id, serial and calibration |
| `config seed_v1` | *(dev/factory)* write a v1 record to demonstrate migration |
| `crash` | *(dev/factory)* force a panic, to prove the handler works |
| `serial set <sn>` | *(factory)* write the serial number |
| `calib vbat <q10>` | *(factory)* battery divider correction |
| `screen info \| main` | switch the OLED page |
| `reboot` | restart |

In the field, holding the key for two seconds does the same as `screen info` —
identity and build on the glass, no laptop needed.

---

## How the version is produced

Nothing is typed by hand. [`scripts/version_flags.py`](scripts/version_flags.py)
runs before every build and injects `FW_SEMVER`, `FW_GIT_HASH`, `FW_BUILD_UTC`,
`FW_GIT_DIRTY` and `FW_HASH_U32` from `git describe`.

The semver comes from the nearest `v*` tag. An image built with uncommitted
changes is marked `-dirty`, `version` prints a warning, and it must not be
deployed: there is no commit to reproduce it from.

Dirtiness is scoped to this project directory, so edits elsewhere in the
monorepo do not falsely mark the firmware dirty.

---

## Layout

```
lib/                pure logic, no Arduino, built and tested on the host
  proto/            frame codec + CRC-16
  cfg/              config schema, bounds, migrations, CRC-32, A/B records
  ringlog/          fixed-capacity record ring
src/
  app/              UART shell, safe mode, event structs
  core/             version, log, config, calibration, POST, health telemetry
  hal/              board pins, battery sense
  tasks/            button, radio, UI, sidetone
test/test_native/   37 host tests
docs/               protocol spec, field checklist, experiments, log dictionary
scripts/            version flags, host test runner, log dict generator, power-cut rig
```

Everything in `lib/` is deliberately free of `Arduino.h` so it links into a host
binary. That is what makes the frame codec, the config bounds, the migration and
the A/B slot rule testable in two seconds without hardware.

---

## Tasks

| Task | Priority | Core | Blocks on |
|---|---:|---:|---|
| `button` | 3 | 1 | polls the key every 5 ms |
| `radio` | 2 | 0 | its queue, 20 ms timeout (doubles as the RX poll) |
| `loopTask` (Arduino `loop()`) | 1 | 1 | the key queue, 1 s timeout |
| `ui` | 1 | 0 | its queue |
| `shell` | 1 | 0 | polls the UART every 20 ms |
| `tone` | 1 | 0 | its queue |

Each bus has exactly one owning **module**: SPI belongs to `radio_task`, I²C to
`ui_task`. Both take a mutex around bus access, because `self-test` re-runs the
POST from the shell task and its radio and display probes go through the owning
module rather than driving the bus themselves. Two masters on one bus is how an
in-flight transaction gets corrupted.

The LED and buzzer are shared between the live key sidetone and the playback
beep, guarded by a mutex taken with a zero timeout — feedback is skipped rather
than allowed to block.

The sidetone has its own task because playing it costs up to 360 ms of
`vTaskDelay`. That delay used to sit inside the radio task, where it stopped RX
polling for the whole beep.

---

## Documents

| Document | For whom |
|---|---|
| [PROTOCOL.md](docs/PROTOCOL.md) | whoever writes the other end — field table, message types, versioning rules, a real frame in hex |
| [FIELD_CHECKLIST.md](docs/FIELD_CHECKLIST.md) | whoever signs off a node before it is deployed |
| [POWER_CUT_EXPERIMENT.md](docs/POWER_CUT_EXPERIMENT.md) | the config-survives-a-power-cut experiment and its results sheet |
| [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | whoever is in the field at three in the morning |
| [log_dict.csv](docs/log_dict.csv) | whoever decodes a binary log dump — regenerate with `python scripts/gen_log_dict.py` |
| [WIRING.md](WIRING.md) | whoever builds one |

`log_dict.csv` is generated from the `LogCode` enum, not maintained by hand: a
hand-kept copy is wrong by the third release, and then an old dump decodes to
the wrong story.

---

## Things that will brick a board

- **Never drive GPIO16.** The board variant header calls it `OLED_RST`, but
  these modules are PICO-D4 and GPIO16 is the embedded flash's chip select.
  Driving it wedges the boot into a silent watchdog loop that looks exactly like
  a dead board. `OLED_RESET_PIN` is `-1` on purpose — leave it.
- **Do not change the partition table** unless you have a reason. The board
  default already provides NVS and two OTA slots.
- **Do not burn eFuses** without running the whole cycle on one sacrificial
  board first. They are permanent.

The firmware itself avoids the two remaining failure modes by design: a torn
config write is caught by the A/B records and their CRC, and a crash loop is
bounded by the boot counter and safe mode.

---

## Not implemented

Stated plainly, because a gap that is written down is a decision and a gap that
is not is a surprise:

- **No OTA.** Over LoRa at SF9 a 480 KB image is about 37 minutes of transmit,
  which is a battery discharge rather than an update. Updating means a cable.
- **No authentication.** `SRC` can be forged and frames can be replayed. The
  CRC catches accidental corruption only. See PROTOCOL.md section 7 for what
  adding a MAC would involve.
- **No duty-cycle enforcement.** A user keying continuously can exceed the EU868
  1 % limit. The health timer is bounded; symbol traffic is not.
- **No listen-before-talk.**
- **No crash dump.** The reset reason and a boot counter survive a reboot, but
  the program counter and stack at the moment of a panic are not captured. The
  `esp32_exception_decoder` monitor filter covers the desk case; a node in the
  field would need a core-dump partition.
