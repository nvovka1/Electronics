# Updating a node over the air

How a node gets a new image without anyone driving to it, and — the part that
actually matters — what stops that from turning it into a paperweight.

Everything here is implemented in [`src/net/ota.h`](../src/net/ota.h) and
[`src/net/ota.cpp`](../src/net/ota.cpp). The fleet service that hands out the
images is [`Backend/LoraFleet`](../../../Backend/LoraFleet).

---

## Why this is not "download and reboot"

An update is the only routine operation that can permanently destroy a device
you cannot reach. Every other failure leaves something running: a bad config is
rejected, a failed POST degrades, a crash reboots. A bad image replaces the only
code that could have fixed it.

So the design question is never "how do I install a new image". It is **"what is
running five minutes after I install one that does not work"**, and there are
four distinct ways that goes wrong:

| What goes wrong | What catches it |
|---|---|
| The download is truncated or corrupted | SHA-256 over every byte, checked before the boot slot is switched |
| The image is for a different board | `hardwareId` in the manifest must equal `FW_HW_ID` |
| The image does not boot at all | The bootloader reverts a `PENDING_VERIFY` image that never confirms itself |
| The image boots but is broken | The trial: a clean POST **and** a successful check-in, or it reverts |

The last one is the one most OTA implementations miss, and it is the one that
produces a node you can see on the shelf and can never fix.

---

## The flash layout

Nothing here was changed for OTA — the board's stock 4 MB layout already has
what it needs, which is why `board_build.partitions = default.csv` is now
written out explicitly in `platformio.ini` rather than left to a default that
somebody could quietly change.

```
0x009000  nvs        20 KB   config A/B, calibration, credentials, ota state
0x00e000  otadata     8 KB   which slot boots, and whether it is on probation
0x010000  app0     1.25 MB   application slot
0x150000  app1     1.25 MB   application slot
0x290000  spiffs   1.375MB   unused
0x3f0000  coredump   64 KB
```

Two application slots is the whole trick. **The running image is never the one
being written.** A node updating from app0 writes app1 and keeps executing app0
the entire time, so pulling the power in the middle of a download is
indistinguishable from never having started one.

The field image is about 1.00 MB, so roughly 280 KB of headroom in a slot. Watch
that number: WiFi and TLS took the image from 26 % of a slot to 78 %, and an
image that no longer fits is an image that cannot be delivered this way.

---

## What happens, in order

### 1. The node asks

Every `report_period_s` (default 300) the network task posts a health report.
The reply carries the answer to a question the node did not have to ask
separately:

```json
{ "serial": "LQ-A41C", "targetFirmwareVersion": "1.1.0", "updateAvailable": true }
```

When `updateAvailable` is true it fetches the manifest from
`GET /api/v1/devices/{serial}/target-firmware`:

```json
{
  "version": "1.1.0",
  "gitHash": "9c41b7e",
  "hardwareId": "ttgo-lora32-v21new",
  "sizeBytes": 1025760,
  "sha256": "360f8334…",
  "downloadUrl": "https://…/api/v1/firmware-releases/…/image"
}
```

`204 No Content` means there is nothing to do. That is the usual answer and the
cheapest one for a node on a battery to handle.

### 2. The gates

Not one byte is downloaded until all of these pass. Each one is a specific
failure somebody has had:

| Gate | Refuses when | Because |
|---|---|---|
| `ota_enabled` | the config says 0 | the operator's switch for a node that must not be touched |
| on trial | this image is itself unconfirmed | updating from an image that has not proved itself loses the known-good one |
| blocked version | this exact version already failed its trial here | otherwise the service re-offers it forever and the battery pays |
| hardware id | `hardwareId != FW_HW_ID` | a neighbouring revision has different pins, and on PICO-D4 one of them is the embedded flash chip select |
| same version | already running it | |
| battery untrusted | the ADC POST bit failed | 330 KB of flash writes on a reading nobody believes |
| battery low | below `ota_vbat_min_mv` (default 3600 mV) | the single most common cause of a bricked device |
| slot size | image larger than the inactive slot | |
| free heap | below 40 KB | TLS buffers and the flash write path want it at the same time |

Note that **safe mode is deliberately not a gate.** A node that has crashed
three times running is exactly the node a new image is meant to cure, and in
safe mode the tasks that were crashing are switched off, so it is the steadiest
this device ever gets.

`ota` shows all of this on one screen:

```
> ota
running   app0  (1280 KB slot)
target    app1  (1280 KB slot)
enabled   yes
vbat gate 3600 mV  (now 3940 mV)
free heap 138944 bytes
state     idle
```

### 3. The download

Streamed in 1 KB chunks straight into the inactive slot, hashed as it goes.
Buffering a third of a megabyte to hash it afterwards is not possible on this
part, and hashing what was *written* rather than what will be read back is what
catches a truncated or tampered image.

The screen shows what is happening, because in the field there is no laptop:

```
 node 1  LQ-A41C
 1.0.0 -> 1.1.0
 UPDATING - keep power
 [##########        ]
  56%                3.9V
```

The battery stays on that screen for the whole download on purpose. It is the
number that decides whether this ends in a new image or in a collection trip.

If the stream stalls for 20 s, or the connection drops, or a write fails, the
update is abandoned. `Update.abort()` leaves the inactive slot full of
half-written bytes and **nothing points at it**, so the node keeps running what
it was running.

### 4. The verification

The SHA-256 of what was written is compared with the manifest. Only if they
match does `Update.end(true)` run, and that single call is the one line in the
whole process that changes which slot boots.

Everything before that line was reversible by doing nothing at all.

### 5. The trial

The node reboots into the new image. The bootloader has marked it
`ESP_OTA_IMG_PENDING_VERIFY`.

Arduino would normally confirm that image the instant `setup()` is reached,
which cancels the safety net before the firmware has demonstrated anything.
This firmware overrides the weak hook so it does not:

```cpp
extern "C" bool verifyRollbackLater() { return true; }
```

The image now has **ten minutes** to earn its place, and it needs two things:

- **A clean critical POST** — specifically, no critical block failing that was
  not *already* failing before the update. A radio that died in the field must
  not make every future image look like a regression; the last confirmed image's
  critical mask is kept in NVS as the baseline.
- **A successful check-in**, at least 60 s after boot. An image that boots and
  self-tests but cannot reach the fleet service is precisely the one nobody can
  ever repair remotely, so it is treated as a failure rather than a success.

While this is happening the node says so, on the screen (`NEW IMAGE ON TRIAL`,
and a `T` in the header), over UART at boot, and in `version`:

```
ota     ON TRIAL - this image reverts in 517 s unless it checks in
        (`ota confirm` to keep it)
```

Three ways out:

| | |
|---|---|
| It confirms | `esp_ota_mark_app_valid_cancel_rollback()`, log `ota_confirmed`, done |
| The window expires | Log `ota_rollback`, record this version as blocked, `esp_ota_mark_app_invalid_rollback_and_reboot()` |
| It panics, hangs the watchdog or browns out | The **bootloader** reverts it with no help from the firmware at all |

That third row is the one that matters most, because it is the only one that
works when the new image is so broken it cannot run its own recovery code.

---

## Doing it by hand

```bash
ota                # slots, gates, trial state
ota check          # ask the service what this node should be running
ota update         # download and install it, gates permitting
ota confirm        # keep an image that is on trial, now
ota rollback       # go back to the previous image deliberately
```

`ota confirm` is for an operator standing in front of a node with a cable who
can see it is fine and does not want it reverted in eight minutes.

`ota rollback` fails on a node that has never been updated over the air — there
is no previous image in the other slot to go back to. That is worth knowing
before you rely on it.

---

## When it goes wrong

**"The node keeps reverting to the old version."**
Read the log: `ota_rollback` carries the reason. `1` = it never checked in —
look at `net`, the credentials or the network changed. `2` = a critical POST
block failed on the new image, which is a real regression and the update should
be withdrawn from the service.

**"The service offers 1.1.0 but the node ignores it."**
`ota` prints `blocked 1.1.0` when that version already failed its trial on this
node. Nothing will install it again until a *different* version is confirmed.
This is deliberate: without it a broken release and an eager service flatten
every battery in the fleet overnight.

**"It refuses with `ota_refused`."**
The argument is the gate index; the fleet dashboard decodes it into a sentence.
`ota` on the node prints the same thing without the lookup.

**"Nothing happens at all."**
`config get` → is `ota_enabled` 1? `net` → is the link up, is the clock synced,
has it ever checked in? An unsynced clock refuses HTTPS outright, because a
certificate is a claim about a window of time and there is no way to check one
without knowing the date.

---

## What is deliberately not here

**No signature.** The image is authenticated by TLS to a pinned root and by a
SHA-256 from a manifest fetched over that same connection. Anyone who can serve
the fleet service can serve an image. Real code signing means secure boot, which
means burning eFuses, which is irreversible — a much larger decision than this
project has earned, and one that turns a mistake into a dead board rather than a
bad update.

**No delta updates.** A full image every time. At 1 MB over WiFi this is
seconds; the complexity of a patch format is not worth it until it is not.

**No update over LoRa.** A megabyte at SF7 is roughly two hours of transmit,
which is a battery discharge rather than an update. The LoRa link carries
telemetry; WiFi carries images.
