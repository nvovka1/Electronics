# Wiring

ESP32 DevKit (WROOM-32) to a SpeedyBee F405 V3 running ArduPilot.

**Three wires.** Both sides are 3.3V logic, so there is no level shifter.

| ESP32 | F405 V3 | Why |
|---|---|---|
| GPIO16 — UART2 RX | **TX** pad of a free UART | The board listens |
| GPIO17 — UART2 TX | **RX** pad of the same UART | Heartbeats and stream requests |
| GND | GND | See below |

TX goes to RX and RX goes to TX. Getting this the wrong way round is the single
most common cause of `NO HEARTBEAT`, and it does no damage — swap them and try
again.

---

## GND is not optional

Even when the ESP32 has its own power supply, the grounds must be joined.

UART is single-ended signalling: a "3.3V" high only means something relative to
a reference both ends agree on. Without a shared ground, the level at the
ESP32's RX pin is undefined — floating, drifting with whatever the two supplies
happen to be doing. The result is nothing at all, or a stream of bytes that
decode to garbage, which looks exactly like a wrong baud rate and gets debugged
as one.

## Power

**Recommended: power the ESP32 separately** — a USB power bank, its own 5V
regulator off the flight battery, anything. Then:

- The flight controller's BEC never has to absorb the ESP32's WiFi current
  spikes, which reach several hundred milliamps and arrive in bursts.
- The brownout that these boards suffer at `WiFi.mode()` — not during transmit,
  at the moment the radio is brought up — cannot take the flight controller's
  5V rail with it.

**Do not join the two 5V rails.** One 5V source at a time. Feeding the ESP32
from the F405's 5V pad *and* from USB at the same time back-feeds the BEC.

If you do power the ESP32 from the F405's 5V pad, put a 470 µF electrolytic
across 5V and GND as close to the ESP32 as you can get it, and expect to find
the limit of the BEC rather than the limit of the firmware.

## Which UART

### Confirmed on this board

Measured on the actual hardware, not derived from a hwdef: **the pad labels map
one-to-one to ArduPilot's serial numbers.** `T1/R1` is `SERIAL1`, `T3/R3` is
`SERIAL3`, and so on.

It was worth confirming because this board **does not follow ArduPilot's default
port roles**. The GPS ships on `SERIAL2`, where the stock default would put it on
`SERIAL3`. That one difference is what makes the defaults table below misleading
if read on its own — and it is also what leaves `SERIAL3` free for this project.

**So: wire to `T3/R3` and configure `SERIAL3`.**

| Parameter | Value |
|---|---|
| `SERIAL3_PROTOCOL` | `2` |
| `SERIAL3_BAUD` | `57` |

Check `SERIAL2_PROTOCOL` still reads `5` afterwards. If it does not, the GPS has
been taken offline, and the symptom is an empty `lat`, `lon` and `utc` for the
whole flight rather than anything that announces itself.

### The general case

ArduPilot's defaults, which decide how much work this is — useful for a
different board, but see above for this one:

| Port | Default role | Default protocol |
|---|---|---|
| `SERIAL0` | USB | MAVLink2 |
| `SERIAL1` | Telem1 | **MAVLink1** |
| `SERIAL2` | Telem2 | **MAVLink1** |
| `SERIAL3` | GPS1 | GPS (5) — **your GPS, leave it alone** |
| `SERIAL4` | GPS2 | GPS (5) |
| `SERIAL5`+ | user | disabled (-1) |

So **Telem1 or Telem2 is the easy answer**: both already speak MAVLink at 57600,
which is this firmware's default baud. Set the protocol to 2 anyway — see below —
but there is nothing to free up.

> **Two different UART2s.** The ESP32 side of this uses *its* UART2 — GPIO16 and
> GPIO17 — and that has nothing to do with the flight controller's `T2/R2` pads.
> The two numbers are unrelated and will not match. Whenever this document says
> UART2 unqualified it means the ESP32's.

Your GPS occupies one port, so pick another. The mapping from the pads
silk-screened `T1/R1`, `T2/R2` … to ArduPilot's `SERIAL1`, `SERIAL2` … is set by
the board's hwdef and is **not worth guessing**:

1. Connect the flight controller to Mission Planner over USB and let it load the
   parameters.
2. **CONFIG → Full Parameter List**, type `SERIAL` in the Search box.
3. Read down the `SERIALn_PROTOCOL` column and work out what each port is doing:

   | Value | Meaning |
   |---|---|
   | `-1` | Disabled — free |
   | `1` / `2` | MAVLink1 / MAVLink2 — a telemetry port, free unless a radio is on it |
   | `5` | GPS — **this is your GPS. Leave it alone.** |
   | `16` | ESC telemetry |
   | `23` | RC input — your receiver |
   | `28` | Lua scripting |

   Anything else is in use by something; find out what before taking it.

4. **The GPS row tells you the pad mapping for free.** You know which pads your
   GPS is physically on, so whichever `SERIALn_PROTOCOL` reads `5` is that pad's
   port number. If your GPS is on the pads marked `2` and `SERIAL2_PROTOCOL` is
   the one reading `5`, the numbering is one-to-one and you can trust `T1/R1` to
   be `SERIAL1`, and so on.
5. Pick a free port, set the three parameters below, then **Write Params** and
   reboot the flight controller.

### If the pad numbering does not line up

It often does not. A GPS on the pads marked `2` while `SERIAL3` is the port
reading `5` means the mapping is offset, and the pad labels cannot be trusted
for any of the others either.

Rather than solve that puzzle, **configure every unused port at once**.
ArduPilot will stream MAVLink on as many ports as you ask it to, and whichever
one your pads turn out to be is then already talking. For every port whose
`_PROTOCOL` reads `-1`, `1` or `2`, set `_PROTOCOL` to `2` and `_BAUD` to `57`.

Leave alone anything reading `5`, `23` or `16`. And if one port already reads
MAVLink while you own no telemetry radio, that is likely an onboard Bluetooth
or wireless module — leave that one too.

Once `status` shows bytes arriving you can put the others back one at a time to
find which it was, or simply leave them; an enabled MAVLink port with nothing
attached costs nothing.

Then set, on the port you picked:

| Parameter | Value | Meaning |
|---|---|---|
| `SERIALn_PROTOCOL` | `2` | MAVLink2 |
| `SERIALn_BAUD` | `57` | 57600, the firmware's default |
| `SERIALn_OPTIONS` | `0` | No inversion, no half duplex |

Reboot the flight controller after changing `SERIALn_PROTOCOL`.

`1` (MAVLink1) would very likely work too — the decoder takes either version —
but `2` is worth setting: MAVLink2 carries the extension fields that some of
these messages have grown, and there is no reason to log a subset of what the
aircraft knows.

**The stream rates do not need setting.** The board asks for the messages it
wants at the rate it wants, with `SET_MESSAGE_INTERVAL`, every time the link
comes up and every fifteen seconds after that. Whatever `SR*_` is set to does
not matter.

## First run

```bash
pio run -e esp32dev -t upload
```

Then open the serial monitor and type `status`:

```
serial      uav-a41c
flight      3, 128 rows written, 0 failed
mav         640 messages, 0 dropped, heartbeat 0.4s ago
armed       disarmed
gps         fix=3 sats=11
battery     16.72 V
net         station, 192.168.1.54, last http 202, 100 rows up
```

Open `http://192.168.1.54` for the board's own page, live telemetry and the CSV
download.

Mission Planner can stay connected over USB the whole time. That is `SERIAL0`,
a different port from the one the ESP32 is on, and ArduPilot streams to each
independently — so you can watch **CTRL+F → MAVLink Inspector** in Mission
Planner and the board's `status` at the same time and see whether they agree.

## When it does not work

| Symptom | Cause |
|---|---|
| `NO HEARTBEAT`, 0 messages | TX/RX swapped, no GND, wrong pads, or `SERIALn_PROTOCOL` is not 2 |
| Messages climbing, `dropped` climbing with them | Wrong baud. Try `set baud 115200` and reboot |
| A burst of drops at power-on, then none | Normal — the UART started mid-frame |
| Heartbeat fine, every GPS column empty | No GPS fix yet. `utc` stays empty until the GPS has one, and so does `lat`/`lon` |
| Board reboots when WiFi comes up | Supply. Separate power, or the 470 µF cap |
| `last http 401` | The API key does not match the service's. `set key <key>` |
| `last http 0` and nothing uploading | Not on the network — check `status`, it says `ap` when it fell back to its own hotspot |

A board that cannot join WiFi raises its own access point, `mavlink-telemetry`
/ `telemetry`. Connect to it and open `http://192.168.4.1` to get the flights
off it in the field.
