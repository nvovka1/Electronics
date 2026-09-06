# lora-queue-release

A two-node LoRa Morse telegraph on LILYGO/TTGO LoRa32 v2.1 boards, built as a
demonstration of what turns firmware that works on a desk into firmware that can
be sent somewhere and left there.

Tap the key once for a dot, twice for a dash. The symbol goes into a queue, out
over LoRa in a framed packet, gets acknowledged, and appears on the other
board's screen. That part is the toy. The rest of this repository is the part
that matters: the node can name itself, log itself, test itself, be reconfigured
over a serial line, survive having its power pulled mid-write, report to a fleet
service over WiFi, and update its own firmware in a way that comes back if the
new image turns out to be bad.

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

## Commissioning a node onto the fleet

The short version is below; [COMMISSIONING.md](docs/COMMISSIONING.md) is the
step-by-step with the reasoning, including where a node's name comes from and
why the API key is not in this repository.

The image ships knowing the commissioning WiFi network and the fleet service
URL. What it does not ship with is the API key, because that is a real secret
and does not belong in a committed file. Give it one over the cable:

```
> net set key dc779e…4419ba45b189
> net report
> net
```

That is the whole commissioning step. The node enrols itself on its first
report — there is no list of serial numbers to type in anywhere.

```
> net
wifi      enabled
ssid      BBC
link      up  ip 192.168.1.57  rssi -54 dBm
clock     synced (UTC)
service   https://electronics-fq9f.onrender.com
api key   set
period    300 s
last ok   12 s ago
logs      74 sent, 0 lost to ring wrap
target    1.0.0  (current)
```

To move a node to a different network or a different service, `net set ssid`,
`net set pass`, `net set url`. All four are stored in their own NVS namespace,
so `config reset` does not wipe them and `net reset` puts back whatever the
image was built with.

The API key is compiled in from `custom_fleet_api_key` in `platformio.ini`, so a
freshly flashed board is already commissioned and `net set key` is only needed to
move one to a different fleet. Every build reports which key it used:

```
version_flags: fleet API key compiled in (64 chars)
```

To supply a key without committing it - which is what a real fleet wants - use
`secrets.local.ini` (gitignored, see the `.example`) or the `LORA_FLEET_API_KEY`
environment variable. Both outrank `platformio.ini`. Note that PowerShell has no
`VAR=value command` form; that is bash syntax and PowerShell reads the whole
thing as a program name:

```bash
$env:LORA_FLEET_API_KEY = "0123...cdef"
~/.platformio/penv/Scripts/pio.exe run -e field
Remove-Item Env:LORA_FLEET_API_KEY
```

**The WiFi password and the fleet API key are both in `platformio.ini`** and are
therefore as public as this repository. That is deliberate for a demo fleet: one
commissioning network, and a key that permits filing health reports and reading
a firmware manifest and nothing else. Both are overridable per node with
`net set`, and the key can be supplied per build instead — see
`secrets.local.ini.example` and [COMMISSIONING.md](docs/COMMISSIONING.md). The
day either guards something real, take it out of this file **and rotate it**:
deleting the line does not reach back through the history.

### What the site gives you

[`Backend/LoraFleet`](../../Backend/LoraFleet) — every enrolled node, its
version and build hash, its POST mask and battery, its decoded ring log, and
which firmware version it is supposed to be running. Assign a version there and
the node picks it up on its next check-in.

---

## Three images from one codebase

The node identity is **not** a build flag — it lives in NVS, so one image serves
any board. What differs between images is what is compiled in.

| | `dev` | `factory` | `field` |
|---|---|---|---|
| Log compiled up to | TRACE (5) | DEBUG (4) | INFO (3) |
| Log echoed to UART | yes | yes | no |
| `config seed_v1`, `crash` | yes | yes | **absent** |
| `serial set`, `calib vbat` | no | yes | no |
| Lives | on the desk | on the conveyor | a year in the field |

`DEBUG` and `TRACE` in the field image are not switched off by a runtime flag —
they are not in the binary, so they can never switch themselves back on.

The field image logs at `INFO` rather than `WARN` since v1.1.0. The ring log is
now uploaded to the fleet service instead of only being read over a cable, and
`INFO` is where the records worth having live: `cfg_saved`, `wifi_up`,
`ota_staged`, `ota_confirmed`. Compiling those out would mean the one record
that proves an update actually took never leaves the device.

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
| `net` | link, clock, service, last check-in, target version |
| `net show` | the four credentials, with the two secrets masked |
| `net set <f> <v>` | `ssid` \| `pass` \| `url` \| `key` |
| `net report` | check in now instead of waiting for `report_period_s` |
| `net reset` | back to the credentials built into the image |
| `config set wifi_tx_dbm <n>` | WiFi transmit power, 2..20 dBm — a supply-current setting as much as a range one |
| `ota` | slots, every gate with its current value, trial state |
| `ota check` | ask the service what this node should be running |
| `ota update` | download and install it, gates permitting |
| `ota confirm` | keep an image that is still on trial |
| `ota rollback` | go back to the previous image |
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

### Where the version actually lives

| Place | Holds | Read by |
|---|---|---|
| The image itself | `-D` flags baked in at compile time | `version`, the OLED, the health frame |
| `.pio/build/<env>/manifest.json` | version, hash, build time, size, SHA-256, HW id | a flashing tool or gateway, before writing anything |
| NVS namespace `calib` | serial number, battery calibration | the node itself; survives `config reset` |
| NVS namespace `cfg` | settings and their `cfg_version` | the node itself |

**Not** in eFuses. The firmware version belongs to the image and cannot drift
from it. eFuse bits only ever go 0 → 1, so a version burnt there could never be
corrected, would spend a fixed budget on every release, and would become a
second source of truth able to disagree with the code that is running.

A note on the app descriptor: ESP-IDF puts an `esp_app_desc_t` at offset `0x20`
of every image, and normally it carries the project version. Under the Arduino
framework it is compiled into a prebuilt library and describes the *framework
builder* — it reads `arduino-lib-builder` / `esp-idf: v4.4.7`, built March 2024,
whatever we compile. That is why the manifest exists. Patching those bytes after
the image is generated would invalidate the image hash, so it is not done.

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
  net/              everything that talks to the fleet service
  tasks/            button, radio, UI, sidetone
test/test_native/   41 host tests
docs/               protocol spec, commissioning, OTA, field checklist,
                    experiments, log dictionary
scripts/            version flags, release manifest, host test runner,
                    log dict generator, power-cut rig
fleet.ini           which fleet these nodes belong to
platformio.ini      how the firmware is built
```

Everything in `lib/` is deliberately free of `Arduino.h` so it links into a host
binary. That is what makes the frame codec, the config bounds, the migration and
the A/B slot rule testable in two seconds without hardware.

### `src/net/` — the fleet uplink

```
netcfg.*            ssid, password, url, api key; NVS namespace "net"
fleet_client.*      the four HTTP calls, JSON in and out, TLS
net_task.*          WiFi lifecycle, the clock, the reporting loop
ota.*               gates, download, SHA-256, the trial and the rollback
root_ca.h           the single certificate this node trusts
```

**Nothing outside this folder talks to the service.** The rest of the firmware
does not know it exists: the radio, the shell, the POST and the config are the
same code they were before there was a fleet, and deleting the folder and its
five call sites in `main.cpp` and `shell.cpp` would leave a working node that
simply reports to nobody.

That boundary is why the network task can block for forty-five seconds on a
sleeping server without a key press being delayed by a millisecond.

### `fleet.ini` — kept apart from `platformio.ini` on purpose

```ini
[fleet]
ssid      = BBC
password  = liza2017
base_url  = https://electronics-fq9f.onrender.com
api_key   = dc779e…ba45b189
```

The two files answer different questions. `platformio.ini` says how the firmware
is **built** — compiler flags, log levels, which test commands exist. `fleet.ini`
says where it will **report**. They change for unrelated reasons, usually by
different people, and pointing a batch of boards at another fleet should not
mean reading past `-Wall`.

`platformio.ini` pulls it in with `extra_configs = fleet.ini` and interpolates
`${fleet.ssid}` and friends, so a plain `pio run` needs no extra arguments.

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
| [COMMISSIONING.md](docs/COMMISSIONING.md) | whoever takes a board from flashed to visible on the dashboard - where the name comes from, what the API key is and the three ways to give a node one |
| [OTA.md](docs/OTA.md) | whoever presses the update button — the gates, the trial, and what reverts an image that does not work |
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
- **Do not change the partition table.** `board_build.partitions = default.csv`
  is now written out explicitly because OTA depends on it: two 1.25 MB
  application slots and an otadata region are what make an update reversible.
  Shrink a slot below the image size and the fleet silently stops being
  updatable. The field image is about 1.00 MB of a 1.25 MB slot — watch that
  number, WiFi and TLS took it from 26 % to 78 % in one release.
- **Do not burn eFuses** without running the whole cycle on one sacrificial
  board first. They are permanent.

The firmware itself avoids the two remaining failure modes by design: a torn
config write is caught by the A/B records and their CRC, and a crash loop is
bounded by the boot counter and safe mode.

---

## Not implemented

Stated plainly, because a gap that is written down is a decision and a gap that
is not is a surprise:

- **No OTA over LoRa.** A megabyte at SF7 is roughly two hours of transmit,
  which is a battery discharge rather than an update. Images go over WiFi; the
  LoRa link carries telemetry. See [OTA.md](docs/OTA.md).
- **No code signing.** An image is authenticated by TLS to a pinned root and by
  a SHA-256 from a manifest fetched over that same connection, which means
  anyone who can serve the fleet service can serve an image. Real signing means
  secure boot, which means burning eFuses, which is irreversible.
- **One pinned TLS root.** GTS Root R4, which is what the fleet service chains
  to today. If its operator ever changes CA, every node stops reporting until
  someone rebuilds — `config set tls_verify 0` is the escape hatch, and it logs
  loudly every time it is used.
- **No per-node authentication.** One shared API key for the fleet. It stops a
  stranger who finds the URL; it does not survive one node being opened up.
- **No authentication on the LoRa link.** `SRC` can be forged and frames can be
  replayed. The CRC catches accidental corruption only. See PROTOCOL.md section
  7 for what adding a MAC would involve.
- **No duty-cycle enforcement.** A user keying continuously can exceed the EU868
  1 % limit. The health timer is bounded; symbol traffic is not.
- **No listen-before-talk.**
- **No crash dump.** The reset reason and a boot counter survive a reboot, but
  the program counter and stack at the moment of a panic are not captured. The
  `esp32_exception_decoder` monitor filter covers the desk case; a node in the
  field would need a core-dump partition.
