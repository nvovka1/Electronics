# Commissioning a node

Taking a board from freshly flashed to visible on the fleet dashboard.

Six steps. Each one says what to type, what a good answer looks like, and — more
usefully — what it means, so that when an answer is different you know which
part is wrong.

---

## What a factory-fresh node already knows

Less than you would think, and that is deliberate.

| | Where it comes from | Needs setting? |
|---|---|---|
| Its name (serial) | derived from the chip's MAC | no |
| Its node id | derived from the same MAC | usually yes |
| WiFi network | compiled into the image | no |
| Fleet service URL | compiled into the image | no |
| Fleet API key | compiled into the image | no, on the demo fleet |

So on this fleet a board out of the box joins the WiFi, finds the service and
is accepted by it, with nothing typed at all. The only step that is genuinely
required is giving it a node id — and that only because 1, 2, 3 is easier to
reason about on air than 191 and 847.

That changes the moment the key becomes a secret rather than a demo credential;
see the next section for what to do then, and step 4 for how a node is given a
key by hand.

---

## Where the device name comes from

Nobody types it, and that is the point: the serial has to match the hardware
exactly, and it is the one field a human is guaranteed to eventually mistype. A
device that names itself cannot be misnamed, and there is no list to keep in
step with reality.

It is the last two bytes of the WiFi MAC, in hex, behind an `LQ-` prefix
(`src/core/calib.cpp`):

```c
esp_read_mac(mac, ESP_MAC_WIFI_STA);
snprintf(s_serial, sizeof(s_serial), "LQ-%02X%02X", mac[4], mac[5]);
```

So a board whose MAC ends `…:27:C4` calls itself **`LQ-27C4`**, on every boot,
for ever, with nothing stored anywhere. Two boards colliding needs their MACs to
share the last two bytes — about 1 in 65,000.

The **node id** — the number that goes on the air in every LoRa frame — comes
from the same two bytes, squeezed into the documented range 1–999:

```c
1 + ((mac[4] << 8 | mac[5]) % 999)
```

For `LQ-27C4` that is `1 + (0x27C4 % 999)` = `1 + (10180 % 999)` = **191**,
which is exactly what that board reported before anyone set it.

Both are only defaults:

- `serial set <sn>` — **factory image only**. For when the board carries a
  printed asset label and the dashboard has to agree with it. Writing it needs
  the factory build precisely because it is not something to change by accident.
- `config set node_id <n>` — any image. Do set this: 1, 2, 3 across the fleet is
  far easier to reason about on air than 191 and 847, and two nodes with the
  same id is a broken network.

`version` prints both, and says when the serial is still MAC-derived:

```
serial  LQ-27C4 (from MAC, not provisioned)
node    1
```

---

## The API key

One shared secret for the whole fleet. It stops a stranger who finds the URL
from enrolling fake devices and filling your log. It does **not** identify an
individual node — compromise one board and you have the key for all of them —
which is why it never guards anything destructive.

It has to be the same string in two places:

| Place | What it is |
|---|---|
| **Render** → your service → Environment → `FLEET__DEVICEAPIKEY` | the authority. The service refuses to start without it outside Development. |
| **The node** | what it sends in the `X-Api-Key` header |

The site never displays it and has no page for it. That is not an omission — a
value a web page can show you is one that anyone reaching the page can read, and
this dashboard has no login.

To create or replace one:

```bash
[Convert]::ToHexString([System.Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLower()
```

Paste it into `FLEET__DEVICEAPIKEY` on Render (Save redeploys, about a minute),
then make sure the nodes carry the same value.

### Where the node's copy comes from

Three sources. `scripts/version_flags.py` picks the first that has a value and
emits exactly one `-D NET_DEFAULT_KEY`, so the macro can never be defined twice
with different values:

| | Source | For |
|---|---|---|
| 1 | `LORA_FLEET_API_KEY` environment variable | a one-off build |
| 2 | `secrets.local.ini`, gitignored | a real fleet, key kept out of git |
| 3 | `api_key` in **`fleet.ini`** | **this demo fleet** |

Every build says which way it went:

```
version_flags: fleet API key compiled in (64 chars)
```

and says so plainly when there is none, in which case each node needs
`net set key` over the cable.

A key written to a node with `net set key` overrides all three, so a board from
a batch can be moved to another fleet without rebuilding.

### On the demo key being in `fleet.ini`

`fleet.ini` holds everything about the world outside the board — the network,
the service, the key — so changing which fleet a batch of boards belongs to is
one file, not a hunt through build flags. It is read by `platformio.ini` via
`extra_configs`.

The key is in it deliberately. This fleet's service holds nothing confidential and
its key permits nothing destructive — it lets you file health reports and read a
firmware manifest — so every board working out of the box is worth more than the
secrecy of that string.

What changes when that stops being true: take the `api_key` line out, **rotate
the key**, and use source 2 instead. The rotation is the part people skip and it is the
part that matters — a value that has been in git is in every clone and every
copy of the history from the commit that added it, and deleting the line does
not reach back. Sources 1 and 2 already outrank `platformio.ini`, so nothing
else has to change.

The WiFi password sits beside it on the same reasoning: one commissioning
network, overridable per node with `net set pass`, and worth nothing outside
this building.

---

## Step by step

### 1. Prove the service and key, before touching hardware

Two minutes, and it permanently separates "the server is wrong" from "the node
is wrong". This is the check to run first every time something stops working:

```bash
curl -s -o /dev/null -w "%{http_code}\n" -H "X-Api-Key: YOUR_KEY" \
  https://electronics-fq9f.onrender.com/api/v1/devices/NOSUCHDEVICE/target-firmware
```

| Answer | Means |
|---|---|
| `404` | ✅ **the key was accepted** — it got past authentication to the lookup |
| `401` | the key is wrong or missing |
| times out | the free hosting tier is asleep; try again, it wakes in tens of seconds |

`NOSUCHDEVICE` deliberately does not exist, so this creates nothing. A `POST` to
`health-reports` would test the same thing but enrol a device you then have to
delete.

### 2. Give it enough power

The single most common reason a node never appears, and no software setting
fixes it. The WiFi radio draws roughly 250 mA when it powers up — before it
transmits anything — and that burst is the largest current the board ever makes.

**Fit the battery.** The board is designed around the cell as its reservoir.
Failing that: a short, thick USB cable straight into the machine, or a powered
hub. Not a long charging cable, not a monitor port.

A supply that cannot carry it shows as:

```
[2685] INFO  net  wifi_connecting  1
Brownout detector was triggered
```

The firmware handles this rather than looping: after one brownout it eases off
(processor to 80 MHz, display blanked, LoRa asleep, lowest transmit power);
after two it stops bringing WiFi up at all and says so in `net`. That keeps the
node alive and answering questions — it does not make the update happen.

### 3. Flash and open the shell

```bash
~/.platformio/penv/Scripts/pio.exe run -e dev -t upload --upload-port COM6
~/.platformio/penv/Scripts/pio.exe device monitor --port COM6
```

Wait for the `>` prompt and for the board to stop rebooting. The shell echoes
what you type — important in the next step, where you paste 64 characters and
the only other feedback is a 401 much later.

### 4. Give it its identity

```
config set node_id 1
```
```
OK  node_id 191 -> 1  (saved, applies after reboot)
```

This is the only step that is always needed. Two nodes sharing an id is a broken
network, and the MAC-derived default is unique but arbitrary.

**The key is already in the image on this fleet**, so there is nothing to type.
`net show` below confirms it. You only need this when the image was built
without one, or when moving a node to a different fleet:

```
net set key dc779eadfe3c3801337b80f92aa32580a8586f192806c28f008e4419ba45b189
```
```
OK  key written (64 chars); it applies on the next check-in
```

Paste the key, never retype it.

```
net show
```
```
ssid   BBC
pass   (set, 8 chars)
url    https://electronics-fq9f.onrender.com
key    (set, 64 chars)
```

**Check it says 64.** The key is never echoed back, so the length is the only
feedback there is — and a half-pasted key produces exactly the same `401` as no
key at all. `(empty)` here on an image that was built with a key means the build
did not pick one up; look for the `version_flags:` line in the build output.

### 5. Make it report

```
net report
net
```

```
wifi      enabled
tx power  13 dBm configured, 13 dBm applied
ssid      BBC
link      up  ip 192.168.1.57  rssi -54 dBm
clock     synced (UTC)
service   https://electronics-fq9f.onrender.com
api key   set
period    300 s
last ok   12 s ago
logs      18 sent, 0 lost to ring wrap
target    not assigned
```

Read it top down; the first line that is wrong is the fault:

| Line | If it is wrong |
|---|---|
| `link down` | SSID or password — `net show`, `net set ssid`, `net set pass` |
| `clock not synced` | no NTP. HTTPS refuses to run without a date, because a certificate is a claim about a window of time and there is no way to check one without knowing when now is. |
| `api key NOT SET` | step 4 did not take |
| `last ok never` | `log dump 20` and read the `report_fail` argument |

### 6. Check the dashboard

<https://electronics-fq9f.onrender.com/devices>

The node appears under the serial `version` printed, with its firmware version,
git hash, POST mask and battery. Its log is on `/logs`, decoded from the numbers
it sent.

Nothing had to be created there first: **the first health report enrols the
device.** There is no "add device" endpoint and no list to maintain.

---

## What survives what

Not obvious, and the source of most "why did my setting come back" questions:

| Stored in | Holds | Cleared by |
|---|---|---|
| `cfg` (A/B records) | every `config` setting | `config reset` |
| `net` | ssid, password, url, api key | `net reset` only — and it falls back to whatever was compiled in, not to nothing |
| `calib` | serial number, battery scale | nothing — it is hardware, not settings |
| `boot` | reboot counters | nothing |
| `ota` | blocked version, POST baseline | a confirmed update |

So `config reset` does **not** un-commission a node, and reflashing does not
either. Only `net reset` gives the credentials back to whatever the image was
built with.

---

## Related

- [FIELD_CHECKLIST.md](FIELD_CHECKLIST.md) — checks 9 and 10 are the signed-off
  version of steps 5 and 6
- [OTA.md](OTA.md) — what happens once a node is reporting and you want to
  update it
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — symptom → check → fix
