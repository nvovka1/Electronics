# MAVLink flight telemetry logger

An ESP32 wired to a SpeedyBee F405 V3 running ArduPilot. It decodes the MAVLink
telemetry stream, writes one CSV row per tick to its own flash, serves those
flights over WiFi, and pushes them to the Initiator service so the CSV can also
be downloaded from <https://initiator-70pm.onrender.com/>.

Firmware: [`ESP32/mavlink`](../../../ESP32/mavlink).
Service: an additive Telemetry area in [`Backend/Initiator`](../../../Backend/Initiator).

---

## What decided the shape

**ArduPilot, not Betaflight.** MAVLink2 both ways, so the board asks for the
messages it wants at the rate it wants with `SET_MESSAGE_INTERVAL` rather than
living with whatever `SR*_` parameters happen to be set to.

**Local first, upload second.** The log is written to flash unconditionally.
The network is a second consumer of a file that already exists, so a WiFi gap,
a sleeping Render instance or a wrong API key is a delay in the record, never a
hole in it.

**Structured rows on the wire.** The board posts decoded rows as JSON, not raw
`.tlog` bytes and not opaque CSV text. The service can then list, summarise and
export, and the CSV you download is generated from the same columns the board
wrote locally.

## Firmware

Four tasks and one shared snapshot behind a mutex.

| Task | Owns | Does |
|---|---|---|
| `mav_task` | UART2 | Decodes MAVLink, folds messages into the snapshot, emits our heartbeat, requests stream intervals |
| `log_task` | the open flight file | Every tick: copy snapshot, render a row, append |
| `net_task` | WiFi and HTTP | STA with AP fallback; uploads unsent bytes of each flight, oldest first |
| `web_task` | the board's HTTP server | Live page, flight list, CSV download, delete |

`log_task` runs on a timer rather than on message arrival. A silent flight
controller therefore produces rows with a rising `linkAgeMs` instead of a gap in
the file, which is the difference between evidence and an absence of evidence.

### Storage

`lib/flightlog` is the boundary; it talks to a `FileSystemPort`, so the host
tests run it against an in-memory fake and the device against LittleFS.

- `/f/<id>.csv` — header plus rows. `id` comes from an NVS counter, because the
  ESP32 has no clock at boot and a flight cannot be named after its start time
  until the GPS has told it what time it is.
- `/f/<id>.mta` — one line, `uploaded=<byte offset>`. Nothing else is stored:
  the row index is the first CSV column, so the uploader reads it out of the
  data rather than keeping a second copy that could disagree.

When free space runs low the oldest *fully uploaded* flight is deleted. If none
is fully uploaded the oldest goes anyway, and that is logged at WARN — losing
the oldest rows beats refusing to record the flight that is happening now.

### Columns

One committed literal, `CSV_HEADER`, shared by the row renderer, the CSV→JSON
converter and the service. A column that is unknown at that tick is written
empty rather than as a zero.

```
index,tMs,utc,armed,mode,gpsFix,sats,hdop,lat,lon,altMsl,altRel,
groundSpeed,airSpeed,climb,heading,cog,roll,pitch,yaw,throttle,
batteryVoltage,batteryCurrent,batteryRemaining,consumedMah,rcRssi,wifiRssi,linkAgeMs
```

`utc` comes from ArduPilot's `SYSTEM_TIME`, which is GPS time — the only real
clock anywhere in the system.

## Service

Additive. Nothing existing changes except one nav link.

- `Initiator.Domain/Telemetry/` — `Flight` and `TelemetrySample`. Deliberately
  not reusing `Device`: an aircraft and an initiator node have unrelated
  lifecycles and sharing a collection would tangle them. There is no `Aircraft`
  document either — an aircraft is exactly the set of flights carrying its
  serial, and a second document saying so would be a second thing to keep in
  step for no question an aggregate cannot answer. `AircraftSummary` is that
  aggregate.
- `Initiator.DataAccess` — `IFlightRepository`, `ITelemetrySampleRepository`.
  A unique index on `(serial, flightId, index)` *is* the idempotency mechanism.
- `Initiator.Web` — `Api/TelemetrySamplesController` (`[ApiKey]`, enrols the
  aircraft on first batch, like `HealthReportsController` does for devices) and
  an MVC `FlightsController` with `Index`, `Details` and a streaming `Csv`.

```
POST /api/v1/aircraft/{serial}/telemetry-samples
{ "flightId": 123, "startedAt": "...", "firstIndex": 450, "samples": [ ... ] }
-> 202 { "flightId": 123, "nextIndex": 470 }
```

One level deep rather than nested under `/flights/{id}`: flight identity in the
body is what lets a single request both open the flight and carry its first
rows. `nextIndex` is the point of the response — the board sets its cursor from
what the service actually holds, so a batch that succeeded but whose response
was lost costs nothing on retry.

Retention: ingest drops the oldest flight per aircraft beyond a configured
count, because the Mongo free tier is finite.

## Failure handling

The network cannot affect the log. An upload failure leaves the cursor untouched
and `log_task` never learns about it. Render's free instance sleeps after ~15
minutes, so the client gets a 20 s timeout and backs off rather than treating
one timeout as broken. A `nextIndex` behind the board's cursor rewinds it;
duplicates hit the unique index and are ignored.

## Testing

Host tests cover the pure parts: folding synthetic
MAVLink frames into a snapshot, rendering a row, converting a row to JSON, and
the flight log's space and cursor logic. Service tests cover ingest
idempotency, the overlap and rewind cases, retention, nulls surviving the round
trip, and CSV formatting under a culture that writes decimal commas.

On this machine `pio test -e native` cannot run — there is no gcc — so
`scripts/run_native_tests.ps1` builds the same sources with MSVC, the same
arrangement the initiator controller already uses.

One test exists only to stop the halves drifting: the CSV header is a single
literal asserted on both sides. Add a column to one and a test fails, instead
of a spreadsheet quietly gaining a shifted column.

## Wiring

Three wires. The ESP32 is powered separately, so the flight controller's BEC
never has to absorb a WiFi current spike.

| ESP32 | F405 V3 |
|---|---|
| GPIO16 (UART2 RX) | TX of a free UART |
| GPIO17 (UART2 TX) | RX of the same UART |
| GND | GND |

GND is not optional — UART is single-ended and without a shared reference the
idle level at the ESP32 pin is undefined, which looks exactly like a wrong baud
rate. The two 5V rails are never joined.
