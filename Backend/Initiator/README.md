# Initiator

Command dispatch and the record of what happened, for the Init/Arm/Fire/Safe
nodes in [`ESP32/initiator`](../../ESP32/initiator).

ASP.NET Core 9 MVC · MongoDB · Docker.

**Live: <https://initiator-70pm.onrender.com/>**

Design: [`docs/superpowers/specs/2026-09-17-initiator-system-design.md`](../../docs/superpowers/specs/2026-09-17-initiator-system-design.md).

> **Free-plan sleep.** The instance sleeps after roughly 15 minutes without
> traffic and takes tens of seconds to wake. A node posting into a sleeping
> instance sees its request time out, so give the firmware a generous HTTP
> timeout and a retry, and do not treat one failed report as the node being
> broken.

---

## What it does

**Devices** — every node that has ever checked in. A node enrols itself on its
first report; there is no list of serial numbers to type in by hand, which is
the one field that has to match the hardware exactly. Each row shows the state
the node is in, how long it has been there, health, battery and RSSI. Anything
not in SAFE floats to the top.

**Commands** — queue INIT, ARM, FIRE or SAFE for a node to collect on its next
poll. Commands the node's last known state would refuse are not queued at all,
so the operator finds out immediately rather than one poll interval later.

**History** — every transition every node has made, from the handheld
controller, from this dashboard, and the ones the nodes made on their own.
Refused commands are here too; they are usually the interesting ones.

**Logs** — the nodes' ring logs. Nodes send codes rather than sentences, so the
text is reconstructed here from the same dictionary that ships with the
firmware.

### What it deliberately does not do

**It does not run the state machine.** The node does. This service holds a copy
of the rules so it can refuse an impossible command early and grey out a button,
but the node judges every command again against its own state and can refuse one
this service thought was fine. When they disagree, the node is right.

**It does not run the auto-arm countdown.** Also the node's. The device page
shows a countdown and labels it an estimate, because duplicating the timer here
would create a second clock that can disagree with the one that decides
anything.

**It cannot reach a node.** Everything is polled. A command queued here arrives
up to one poll interval later, which is why the handheld controller exists.

---

## The state machine

| From \ Command | `INIT` | `ARM` | `FIRE` | `SAFE` | *timer* |
|---|---|---|---|---|---|
| **SAFE**  | → INIT          | reject    | reject    | — | — |
| **INIT**  | restarts timer  | → ARMED   | reject    | → SAFE | **→ ARMED after 5 min** |
| **ARMED** | reject          | —         | → FIRE    | → SAFE | — |
| **FIRE**  | reject          | reject    | —         | → SAFE | — |

- **Boot is always SAFE**, never restored from storage.
- **SAFE is the only revoke path**, and it is accepted from every state.
- **Re-sending the current state's command succeeds** and changes nothing — the
  radio retries, and a lost ACK must not turn a success into a failure. INIT
  while in INIT is the one exception: it restarts the countdown.
- **ARM does not expire.** Combined with the INIT timer, a node that is sent
  INIT and then forgotten arms itself after five minutes and stays ARMED. Only a
  SAFE clears it.

`NodeStateMachine` in `Initiator.Domain` is the only place these rules live on
this side. The firmware holds the same table in C++, and they are checked
against each other by a test — see **Tests**.

---

## Running it

### Locally

Secrets go in user-secrets, never in a file in the repo:

```bash
cd src/Initiator.Web
dotnet user-secrets set "Mongo:ConnectionString" "mongodb://localhost:27017"
dotnet user-secrets set "Initiator:DeviceApiKey" "some-dev-key"
dotnet run
```

`Initiator:SeedDemoData` is on in Development and fills an empty database with
four plausible nodes — one in SAFE, one part-way through a sequence with its
countdown running, one ARMED, and one that has gone silent while not in SAFE —
so the dashboard can be seen working before any hardware exists. It only runs
when the `Devices` collection is empty, so it can never overwrite real
check-ins.

### Docker Compose

```bash
cp .env.example .env     # then fill in the two secrets
docker compose up --build
```

Then <http://localhost:8081>. Port 8081 rather than 8080 so this runs alongside
LoraFleet without a collision.

To work against the bundled database instead of your own:

```bash
docker compose --profile local-db up
# and set MONGO_CONNECTION_STRING=mongodb://mongo:27017 in .env
```

### Deploying

The Dockerfile's `COPY src/...` paths are relative to the solution root, so the
**build context must be `Backend/Initiator`** — on Render that is the service's
Root Directory. With the repository root as the context every `COPY` fails with
`"/src": not found`, and the error does not say why.

| Setting | Value |
|---|---|
| Root Directory | `Backend/Initiator` |
| Runtime | Docker |
| Dockerfile Path | `src/Initiator.Web/Dockerfile` |
| Health Check Path | `/healthz` |

The deployed instance is <https://initiator-70pm.onrender.com/> — a different
Render service from LoraFleet's, with its own database.

Two variables are prompted for rather than committed:

| Variable | Value |
|---|---|
| `MONGO__CONNECTIONSTRING` | the Mongo URI, with credentials |
| `INITIATOR__DEVICEAPIKEY` | `openssl rand -hex 32` |

The double underscore is how the .NET configuration binder reads a nested key
out of an environment variable: `MONGO__CONNECTIONSTRING` becomes
`Mongo:ConnectionString`.

---

## Device API

All device-facing endpoints require the fleet key:

```
X-Api-Key: <INITIATOR__DEVICEAPIKEY>
```

States, commands and reasons travel as **numbers**, not names — the same bytes
the LoRa protocol uses:

| | |
|---|---|
| `NodeState` | `0` SAFE · `1` INIT · `2` ARMED · `3` FIRE |
| `CommandType` | `1` INIT · `2` ARM · `3` FIRE · `4` SAFE |
| `CommandSource` | `0` lora · `1` api · `2` timer |
| `RejectReason` | `0` ok · `1` bad transition · `2` bad target · `3` replay · `4` bad command |

### `POST /api/v1/devices/{serial}/health-reports`

The periodic report. The first one from a serial enrols the device.

```bash
curl -X POST http://localhost:8081/api/v1/devices/IN-A41C/health-reports \
  -H "X-Api-Key: $KEY" -H "Content-Type: application/json" \
  -d '{
        "nodeId": 2,
        "hardwareId": "ttgo-lora32-v21",
        "firmwareVersion": "0.1.0",
        "protocolVersion": 1,
        "uptimeSeconds": 3600,
        "reboots": 1,
        "batteryDeciVolts": 39,
        "lastRssi": -91,
        "postMask": 0,
        "loraLinkUp": true,
        "state": 1,
        "stateTimestampMs": 41000,
        "autoArmTimeoutSeconds": 300
      }'
```

Answers `202` with the state this service believes the node is in, so a node
whose state event went missing can tell on the next report rather than waiting
for its next transition:

```json
{ "serial": "IN-A41C", "lastSeenAt": "...", "state": 1 }
```

`batteryDeciVolts` is tenths of a volt. **Zero means the reading is untrusted** —
the node's ADC self-test failed — not that the battery is flat.

### `POST /api/v1/devices/{serial}/state-events`

Every transition, accepted or refused. A batch, because nodes buffer these while
offline and flush them when the connection returns.

```bash
curl -X POST http://localhost:8081/api/v1/devices/IN-A41C/state-events \
  -H "X-Api-Key: $KEY" -H "Content-Type: application/json" \
  -d '{"events":[
        {"timestampMs":41000,"bootCount":1,"fromState":0,"toState":1,
         "command":1,"source":0,"accepted":true,"reason":0},
        {"timestampMs":341000,"bootCount":1,"fromState":1,"toState":2,
         "command":null,"source":2,"accepted":true,"reason":0}
      ]}'
```

The second one is an auto-arm: `source` 2 and no command, because nothing
commanded it.

`bootCount` is not decoration. `timestampMs` is the node's own monotonic clock
and restarts at zero on every boot, so the two together are what order a
report — without the boot count, a node that has just rebooted looks like
ancient history and its reports are ignored forever.

Answers `202` with `{ "stored": n, "discarded": n }`. Events carrying a state or
command this build does not know are discarded rather than stored, so nothing
renders as `state200`. The node uses `stored` to decide what it may drop from
its buffer.

### `GET /api/v1/devices/{serial}/commands/next`

`200` with a command, or `204` when there is nothing — the common answer, and
the cheapest for a device on a battery. Claiming is atomic, so two polls in
flight cannot both be handed the same command.

```json
{ "commandId": "6710...", "command": 1, "queuedAt": "..." }
```

### `POST /api/v1/devices/{serial}/commands/{commandId}/result`

```bash
-d '{"accepted":true,"reason":0,"state":1,"timestampMs":41000,"bootCount":1}'
```

Always answers `202`, even for an id that resolved nothing — a node retrying a
post it never saw acknowledged is the common cause, and a `404` would make it
retry something that can never succeed.

### `POST /api/v1/devices/{serial}/log-records`

```bash
-d '{"records":[{"timestampMs":41,"level":3,"tag":4,"code":50,"arg":2}]}'
```

---

## Layout

```
src/
  Initiator.Domain/       entities and the rules over them; no dependencies
    States/               NodeState, CommandType, NodeStateMachine, StateEvent
    Commands/             Command, CommandStatus, CommandPolicy
    Devices/              Device, DeviceHealth, DeviceStatusPolicy
    Logs/                 LogRecord, LogDictionary
  Initiator.DataAccess/   Mongo context, class maps, repositories, index setup
  Initiator.Web/          MVC controllers and views, device API, services
tests/
  Initiator.Tests/        124 tests
```

`LogDictionary` is a contract with the firmware's `LogCode` enum. Change a
number on one side without the other and every stored record decodes to the
wrong story — silently, which is the worst way for it to happen.

Nothing here references `Backend/LoraFleet`. The shapes that resemble it are
copies, so the two can change independently.

---

## Tests

```bash
dotnet test
```

Most run against pure logic: the transition table asserted cell by cell against
the spec, the command lifecycle, the status policy, the log dictionary's packed
arguments.

The rest start a real `mongod` (EphemeralMongo, no Docker needed), because the
parts that are easy to get wrong cannot be checked against a mock — a mock
agrees with whatever the code happens to do. Those are the BSON mapping, the
upsert that must leave the operator's fields alone, the conditional state write,
and the atomic claim.

Three worth knowing about:

- `A_check_in_never_overwrites_what_the_operator_set` — with a naive replace, a
  node reporting in would wipe its own notes and its retirement.
- `A_report_that_arrives_late_does_not_put_the_state_back` — a health report in
  flight while the node armed itself would otherwise return the dashboard to
  INIT and leave it there.
- `A_command_is_only_ever_handed_out_once` — two polls at once, or two instances
  behind a load balancer, must not both be given the same command.

`EndToEndTests` boots the real host against a real database and drives the whole
round trip — enrol, queue from the dashboard's own form, poll, report, auto-arm —
and loads every page, because a null in a view is a 500 rather than a compiler
error.

---

## Security notes

- The connection string and the API key are never in a committed file.
- A shared key stops a stranger who finds the URL from enrolling devices and
  filling the log. It does not authenticate an individual node — compromise one
  and you have the key for all of them. **Anyone holding it can queue a command
  for any enrolled node.** The node's own state machine is what limits the
  damage; the key is not a substitute for keeping this service off the open
  internet.
- **There is no user authentication on the dashboard.** Anyone who can reach the
  URL can queue commands and delete devices. Put it behind an authenticating
  proxy before it holds anything that matters.
- `.dockerignore` **must keep LF line endings**. With CRLF, Docker treats the
  carriage return as part of each pattern, none of them match, and `.env` ends
  up inside an image layer.
- The container runs as a non-root user and serves plain HTTP; TLS belongs to
  whatever sits in front of it.
- The repository root `.gitignore` has `[Ll]ogs/`, a stock Visual Studio rule
  that also matches this project's `Logs` source folders. `.gitignore` here
  lifts it for `src/`. Remove that and the project builds on your machine and
  fails in the container with "the namespace Logs does not exist".
