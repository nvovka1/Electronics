# LoraFleet

Fleet management for the LoRa nodes in [`ESP32/lora-queue-release`](../../ESP32/lora-queue-release):
every enrolled device, its health, its log, and which firmware version it is
supposed to be running.

ASP.NET Core 9 MVC · MongoDB · Docker.

---

## What it does

**Devices** — every node that has ever checked in. A device enrols itself on its
first report; there is no list of serial numbers to type in by hand, which is
the one field that has to match the hardware exactly. Each row carries status,
firmware version and hash, POST mask, battery, RSSI, reboot count and how long
it has been quiet.

**Logs** — the fleet's ring-log records, filtered by device, severity and tag.
Nodes send codes rather than sentences (twelve bytes a record instead of about
sixty), so the text is reconstructed here from the same dictionary that ships
with the firmware.

**Firmware** — the release shelf. Upload the `manifest.json` a firmware build
writes next to its image, optionally with the `.bin`. The image's SHA-256 is
checked against the manifest before anything is stored. Assign a target version
to a device and the fleet list shows who is out of date.

### What it deliberately does not do

**It does not push firmware to devices.** The firmware has no OTA client — over
LoRa a third of a megabyte is close to forty minutes of transmit, which is a
battery discharge rather than an update. Flashing is a cable job. What this
service provides is the decision: which node should be on which version, and
which ones are not. The `target-firmware` endpoint and the image download are
already shaped for an OTA client on the day one exists.

---

## Running it

### Locally

Secrets go in user-secrets, never in a file in the repo:

```bash
cd src/LoraFleet.Web
dotnet user-secrets set "Mongo:ConnectionString" "mongodb+srv://user:pass@cluster.mongodb.net/"
dotnet user-secrets set "Fleet:DeviceApiKey" "some-dev-key"
dotnet user-secrets set "SeedDemoData" "true"
dotnet run
```

`SeedDemoData` fills an empty database with four plausible nodes — one healthy,
one degraded, one missing, one running a dirty build — so the dashboard can be
seen working before any hardware exists. It only runs when the `Devices`
collection is empty, so it can never overwrite real check-ins.

### Docker Compose

```bash
cp .env.example .env     # then fill in the two secrets
docker compose up --build
```

Then <http://localhost:8080>.

To work against a local database instead of Atlas:

```bash
docker compose --profile local-db up
# and set MONGO_CONNECTION_STRING=mongodb://mongo:27017 in .env
```

### Render

`render.yaml` is a blueprint. Set the service's **root directory** to
`Backend/LoraFleet` — the Dockerfile's `COPY` paths are relative to the solution
root, not to the web project.

Two variables are marked `sync: false` and are prompted for once in the
dashboard rather than committed:

| Variable | Value |
|---|---|
| `MONGO__CONNECTIONSTRING` | the Atlas URI, with credentials |
| `FLEET__DEVICEAPIKEY` | `openssl rand -hex 32` |

The double underscore is how the .NET configuration binder reads a nested key
out of an environment variable: `MONGO__CONNECTIONSTRING` becomes
`Mongo:ConnectionString`.

**Atlas network access:** Render's outbound addresses are not fixed on the free
plan. Either allow `0.0.0.0/0` in Atlas Network Access — acceptable only because
the database user is scoped and its password is strong — or move to a plan with
static outbound IPs.

**Free plan sleep:** the instance sleeps after roughly 15 minutes without
traffic and takes tens of seconds to wake. A node posting a health report into a
sleeping instance sees its request time out, so give the devices a generous HTTP
timeout and a retry.

---

## Device API

All device-facing endpoints require the fleet key:

```
X-Api-Key: <FLEET__DEVICEAPIKEY>
```

A shared key is the weakest thing still worth having: it stops a stranger who
finds the URL from enrolling devices and filling the log. It does not
authenticate an individual node — compromise one and you have the key for all of
them — which is why it never guards anything destructive.

### `POST /api/v1/devices/{serial}/health-reports`

The facts the firmware's `version` and `health` commands print over UART. The
first report from a serial enrols the device.

```bash
curl -X POST https://electronics-fq9f.onrender.com/api/v1/devices/LQ-A41C/health-reports \
  -H "X-Api-Key: $FLEET_KEY" \
  -H "Content-Type: application/json" \
  -d '{
        "nodeId": 1,
        "hardwareId": "ttgo-lora32-v21new",
        "firmwareVersion": "1.0.0",
        "firmwareHash": "59b0d70",
        "firmwareIsDirty": false,
        "buildType": "field",
        "protocolVersion": 1,
        "configVersion": 2,
        "uptimeSeconds": 86400,
        "reboots": 3,
        "lastCrashCode": 1,
        "batteryDeciVolts": 39,
        "lastRssi": -97,
        "postMask": 0
      }'
```

Answers `202` with the assigned target version, so a node learns it is out of
date on the same round trip:

```json
{ "serial": "LQ-A41C", "lastSeenAt": "...", "targetFirmwareVersion": "1.1.0", "updateAvailable": true }
```

`batteryDeciVolts` is tenths of a volt. **Zero means the reading is untrusted** —
the node's ADC self-test failed — not that the battery is flat.

### `POST /api/v1/devices/{serial}/log-records`

```bash
curl -X POST https://electronics-fq9f.onrender.com/api/v1/devices/LQ-A41C/log-records \
  -H "X-Api-Key: $FLEET_KEY" \
  -H "Content-Type: application/json" \
  -d '{"records":[{"timestampMs":41,"level":2,"tag":2,"code":26,"arg":1}]}'
```

`timestampMs` is that node's own monotonic clock — milliseconds since it booted.
Nodes have no RTC, so it is never wall-clock time and is not comparable across a
reboot. The absolute time is stamped on receipt.

### `GET /api/v1/devices/{serial}/target-firmware`

`200` with a manifest when the node should be on a different version, `204` when
there is nothing to do — the common answer, and the cheapest for a device on a
battery to handle.

### `GET /api/v1/firmware-releases/{releaseId}/image`

The `.bin`. Supports range requests, so an interrupted download resumes instead
of starting again. The SHA-256, version and hardware id also travel in response
headers, so a client streaming straight to flash can verify without buffering
the whole image.

---

## Layout

```
src/
  LoraFleet.Domain/       entities and the rules over them; no dependencies
    Devices/              Device, DeviceHealth, PostBlock, DeviceStatusPolicy
    Logs/                 LogRecord, LogDictionary
    Firmware/             FirmwareRelease
  LoraFleet.DataAccess/   Mongo context, class maps, repositories, index setup
  LoraFleet.Web/          MVC controllers and views, device API, services
tests/
  LoraFleet.Tests/        44 tests
```

`LogDictionary` is a contract with the firmware's `LogCode` enum. Change a
number on one side without the other and every stored dump decodes to the wrong
story — silently, which is the worst way for it to happen. The firmware's own
copy is generated from the enum by `scripts/gen_log_dict.py` for that reason.

---

## Tests

```bash
dotnet test
```

33 run against pure logic — status policy boundaries, the log dictionary's
packed arguments, POST mask decoding. The other 11 start a real `mongod`
(EphemeralMongo, no Docker needed) because the part that is easy to get wrong is
the BSON mapping: a natural string key, computed properties that must not be
persisted, and an upsert that has to leave the operator's fields alone. A mock
would simply agree with whatever the mapping happened to do.

The test worth knowing about is
`A_check_in_never_overwrites_what_the_operator_set` — with a naive replace, a
node reporting in would wipe its own target version and notes.

---

## Security notes

- The connection string and the API key are never in a committed file. They come
  from user-secrets locally and from the environment in a container.
- `.dockerignore` **must keep LF line endings**. With CRLF, Docker treats the
  carriage return as part of each pattern, none of them match, and `.env` ends
  up inside an image layer.
- The container runs as a non-root user and serves plain HTTP; TLS belongs to
  whatever sits in front of it.
- There is no user authentication on the dashboard itself. Anyone who can reach
  the URL can assign firmware and delete devices. Put it behind an
  authenticating proxy, or add auth, before it holds anything that matters.
