# Changelog

Three lines in plain language per release: what changed, what broke, what
somebody upgrading has to do.

## v1.1.1 - 2026-09-06

**What changed.** Two things, both about the WiFi transmitter's appetite for
current. The radio now transmits at 13 dBm instead of the 19.5 it defaults to,
which roughly halves the peak draw during an association and is ample indoors;
the level is a new setting, `wifi_tx_dbm`, so a node at the edge of coverage can
be turned back up without a rebuild. And the node now counts brownout resets
separately from other abnormal boots: after one it drops to the lowest power the
radio has and waits five seconds before associating, and after three it stops
bringing WiFi up at all until somebody power-cycles the board.

**What broke.** Nothing, but one behaviour is worth stating plainly because it
was a real defect: before this release a board whose supply could not carry the
transmit burst would brown out, reboot, brown out again, forever. Safe mode did
not help - it deliberately leaves the uplink on so a crashing node can still be
cured remotely, which is exactly wrong when the uplink is the cause. The
brownout hold is the way out of that loop.

**Upgrading.** `cfg_version` goes to 4, migrated automatically from 3, 2 or 1
with every tuned value carried across. Note the direction of this migration:
a node coming from v1.1.0 comes back **quieter** than it was, which is the point.
If a node stops associating after the update, `config set wifi_tx_dbm 17` puts
it back. The proper fix for a brownout is still a battery or a better cable -
the firmware can only make the spike smaller, not make the supply bigger.

## v1.1.0 - 2026-09-06

**What changed.** The node joined a fleet. It associates with WiFi, reports its
health and its ring log to the LoraFleet service every `report_period_s`, and
can install a firmware image the service assigns it. The update is built so a
bad image comes back: the running slot is never the one written, every byte is
SHA-256 checked before the boot slot is switched, and a new image is on trial
for ten minutes afterwards - it keeps its place only by passing a clean critical
POST and checking in with the service, and the bootloader reverts it on its own
if it cannot even get that far. `config` gained five settings and `cfg_version`
went to 3; the WiFi and service credentials live in a separate NVS namespace
because they are provisioning rather than tuning, and because growing the A/B
config record would have made every existing node fall back to defaults. New
commands: `net`, `net show|set|report|reset`, `ota`, `ota check|update|confirm
|rollback`. The screen carries an uplink indicator and, during an update, the
node number, both version numbers, a progress bar and the battery.

**What broke.** Nothing on the air - the LoRa protocol is untouched at `VER 1`.
Two things to know before flashing: the image went from 26 % to 78 % of an
application slot, because WiFi, TLS and HTTP are most of it; and `verifyRollback
Later()` is now overridden, so an image installed over the air that never
confirms itself will be reverted on its next reboot. That is the point, but it
means a future release must never remove the confirmation path without removing
the override with it.

**Upgrading.** Flash by cable once, from v1.0.0 - a v1.0.0 node has no OTA
client to reach. Existing settings survive: the stored v2 record is migrated to
v3 on the first boot and every tuned value is carried across by name, with the
five new fields taking the firmware's defaults. The uplink comes up enabled but
finds nothing to talk to until the node is given an API key with `net set key`.
Rolling back to v1.0.0 means a cable, and it means the node stops reporting.

## v1.0.0 — 2026-09-05

**What changed.** The demo became firmware that can be deployed. A `version`
command that reports a build identity taken from git; a 256-record ring log with
levels, where DEBUG and TRACE are absent from the field image rather than merely
switched off; `config get/set/reset` in NVS with bounds validated on the device,
a `cfg_version` and a working v1→v2 migration; a six-block power-on self-test
whose result is a bitmask carried in the log, on the screen and in telemetry; a
real framed protocol with sequence numbers, CRC, ACK and retries in place of a
single raw ASCII byte; a boot counter and safe mode; a task watchdog; and a
low-battery gate that refuses to write flash on a flat pack.

**What broke.** Everything on the air. A v1.0.0 node cannot talk to a v0.1.0
node at all: the old firmware put one unframed byte on the wire with the
library's stock sync word, and the new one sends a 10-byte-overhead frame on
sync word `0x2B`. Both boards must be updated together. The build environments
`boardA` and `boardB` are gone — node identity now lives in NVS, so use `dev`,
`factory` or `field` and set `node_id` with a command.

**What to do when upgrading.** Flash both boards. On the second one, run
`config set node_id 2` — without it both fall back to a MAC-derived id and,
although they will not usually collide, nothing guarantees which is which. Run
through [docs/FIELD_CHECKLIST.md](docs/FIELD_CHECKLIST.md) before deploying.

## v0.1.0 — 2026-09-05

Baseline. A three-task FreeRTOS Morse-over-LoRa demo: button → queue → radio →
OLED, with latency instrumentation at every hop. Works on a desk. No version, no
log levels, no persisted config, no self-test, and one raw ASCII byte on the
air.
