# Changelog

Three lines in plain language per release: what changed, what broke, what
somebody upgrading has to do.

## v1.0.0 — unreleased

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
