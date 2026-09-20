# MAVLink flight telemetry logger

An ESP32 on a spare UART of a SpeedyBee F405 V3 running ArduPilot. It decodes
the MAVLink stream, writes a CSV row twice a second to its own flash, serves
those flights over WiFi, and pushes them to the Initiator service so the same
CSV can be downloaded from the site.

Wiring: [`WIRING.md`](WIRING.md) — three wires, and GND is one of them.
Design: [`docs/superpowers/specs/2026-09-20-mavlink-telemetry-design.md`](../../docs/superpowers/specs/2026-09-20-mavlink-telemetry-design.md).

```bash
pio run -e esp32dev -t upload          # flash
pio device monitor -e esp32dev         # the shell
powershell -File scripts\run_native_tests.ps1   # host tests, no board
```

---

## The idea

**Local first.** The log is written to flash unconditionally. The network is a
second consumer of a file that already exists, so a WiFi gap, a sleeping Render
instance or a wrong API key is a delay in the record, never a hole in it. A
flight flown out of range arrives late; it does not arrive short.

**Unknown is not zero.** A field nobody has reported is an empty CSV cell, a
`null` on the wire, and a null in the database. A battery reading 0.00 V and a
battery nobody has mentioned are completely different situations, and once
they are both zero in the file there is no getting them apart again.

**The board asks for what it wants.** ArduPilot takes `SET_MESSAGE_INTERVAL`, so
the stream rates are this firmware's decision rather than whatever `SR*_`
parameters a board happens to carry. The request is repeated every fifteen
seconds, because ArduPilot forgets it across its own reboot.

## Five tasks

| Task | Owns | Does |
|---|---|---|
| `mav` | UART2 | Decodes MAVLink into the snapshot, sends our heartbeat, requests intervals |
| `rec` | the open flight file | One row per tick, whatever else is happening |
| `net` | WiFi and HTTP | Joins, uploads, moves the cursor |
| `web` | port 80 | The board's page and the CSV download |
| `shell` | the console | Commissioning |

Only two things are shared — the snapshot and the flight log — and both live
behind their own lock in [`src/store.cpp`](src/store.cpp). Nothing else in this
firmware needs one.

`rec` runs on a timer rather than on message arrival. A flight controller that
has stopped talking then produces rows whose `linkAgeMs` climbs, which is
evidence. The alternative is a gap in the file, which is an absence of evidence
and indistinguishable from the board having been switched off.

## Storage

Two files per flight, on LittleFS:

```
/f/<id>.csv    the header and the rows
/f/<id>.mta    one line, "uploaded=<byte offset>"
```

`id` is the boot count: a power-on is a new flight, and with no clock until the
GPS provides one, a counter that survives the reboot is the only thing available
to name it by.

The meta file holds nothing else on purpose. The row index is the first CSV
column, so the uploader reads it out of the data rather than keeping a second
copy that can disagree.

The partition table gives 2 MB to the filesystem by dropping the OTA slot —
about five hours at 2 Hz. When space runs low the oldest **fully uploaded**
flight is deleted; if none is fully uploaded the oldest goes anyway and that is
logged at WARN. Losing the oldest rows beats refusing to record the flight that
is happening now.

## Columns

```
index,tMs,utc,armed,mode,gpsFix,sats,hdop,lat,lon,altMsl,altRel,groundSpeed,
airSpeed,climb,heading,cog,roll,pitch,yaw,throttle,batteryVoltage,
batteryCurrent,batteryRemaining,consumedMah,rcRssi,wifiRssi,linkAgeMs
```

`utc` comes from ArduPilot's `SYSTEM_TIME`, which is GPS time — the only real
clock anywhere in this system, and empty until the GPS has a fix.

`wifiRssi` is not from MAVLink. It is the board's own signal, and it is in the
row because "the upload stopped" and "the aircraft flew out of WiFi range" are
one event seen from two sides.

**`CSV_HEADER` is a contract** with `TelemetryCsv.Header` in the Initiator
service. A host test asserts the literal against the column table here, and a
service test reads this file and asserts the same literal there. Add a column to
one half only and a build fails — the alternative is a spreadsheet that quietly
gains a shifted column and gets believed.

## The upload

```
POST {base}/api/v1/aircraft/{serial}/telemetry-samples
X-Api-Key: ...
{ "flightId": 123, "firstIndex": 450, "samples": [ ... ] }

202 { "flightId": 123, "nextIndex": 470, "stored": 20 }
```

`nextIndex` is the point of the response. The board sets its cursor from what
the service says it holds, not from its own arithmetic, so a batch that
succeeded but whose response was lost costs one repeated request. A `nextIndex`
behind the board rewinds it and the flight is sent again; duplicates hit a unique
index on the far side and are discarded.

Render's free instance sleeps after about fifteen minutes and takes tens of
seconds to wake, so the HTTP timeout is 20 s and a failure backs off rather than
being treated as broken.

## The shell

Per-board settings live in NVS, so one image serves every board.

```
status                    what the board is doing
flights                   every flight on disk
head <id> [rows]          first rows of a flight
tail <id> [rows]          last rows
rm <id>                   delete one flight
set serial <name>         board name; the aircraft's serial on the site
set wifi <ssid> <pass>
set url <base-url>
set key <api-key>
set baud <rate>           flight controller serial baud
set rate <hz>             rows per second, 1..10
set upload on|off         pause uploading without stopping recording
reset                     settings back to the built-in defaults
reboot
```

A board nobody has named is named after its own chip MAC — `uav-a41c` — so a
factory-fresh board enrols itself correctly without anybody typing the one field
that has to match.

Defaults for a fresh board live in [`fleet.ini`](fleet.ini): network, service URL
and API key. Anything set with `set` wins and survives a reflash.

## Tests

`scripts/run_native_tests.ps1` builds everything in `lib/` with MSVC and runs it
— 22 tests, about a second, no board. (`pio test -e native` does the same thing
on a machine that has gcc; this one does not.)

They cover the parts that are pure: folding synthetic MAVLink frames into a
snapshot, rendering a row, converting a row to JSON, and the flight log's space
and cursor logic — including what happens when the disk is full, which is why
the flight log is written against a `FileSystemPort` and tested against an
in-memory fake rather than a board's flash.
