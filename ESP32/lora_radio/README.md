# ESP32 LoRa Band Scanner

Listens on the 868 MHz band with a LILYGO T3 v1.6.1 and shows what it hears on the
board's OLED — live signal strength, decoded packets, an RSSI graph over time, and
a noise-floor sweep across the whole band. An analog joystick drives everything:
**left/right tunes, up/down changes page**, and the board's BOOT button is "click".

## Read this first: what LoRa can and can't do

Worth setting expectations, because "scanner" means something narrower here than on
a broadcast radio.

The SX1276 chip demodulates **only** packets whose spreading factor, bandwidth,
coding rate and sync word match its own settings *exactly*. There is no wideband
demodulation and no promiscuous mode — no LoRa chip has one. So this project does
two genuinely different things:

- **Noise-floor sweep — always works.** Stepping across 863–870 MHz and reading raw
  signal strength shows you where there is energy on the band. This works on day
  one regardless of what anyone is transmitting, and it's the feature that will
  actually show you something interesting.
- **Packet decode — only when the settings match a real transmitter.** The defaults
  are the LoRaWAN EU-868 parameters, which gives a real chance of catching utility
  meters and sensors in a city. **An empty packet page is a normal result, not a
  fault.**

Two more honest limits:

- **LoRaWAN payloads are AES-encrypted.** You will see length, signal strength, SNR
  and hex bytes — never readable text.
- **A fixed spreading factor only catches that spreading factor.** Real LoRaWAN
  devices move across SF7–SF12, so try the SF7 / SF9 / SF12 presets in turn.

## PlatformIO board

`ttgo-lora32-v21` — this is the right id for boards marked *T3_V1.6* and
*T3_V1.6.1*, despite the `v21` in the name. If you have an older board, switch it
in [`platformio.ini`](platformio.ini) to `ttgo-lora32-v2` (V2.0) or
`ttgo-lora32-v1` (V1.0/V1.3).

Libraries, all resolved automatically by PlatformIO:

- `sandeepmistry/LoRa` — SX1276 driver
- `adafruit/Adafruit SSD1306` + `adafruit/Adafruit GFX Library` — the OLED

## Hardware

- LILYGO T3 v1.6.1, 868 MHz (the board is marked *868/915*; 868 is the legal ISM
  band in the EU and Ukraine)
- An 868 MHz antenna — **attached before you power up**
- Any 2-axis analog thumbstick module (KY-023 or equivalent)
- 4 female-to-female jumper wires
- Optional: an 18650 cell for portable use; the board charges it over USB

Full pin tables and warnings are in [WIRING.md](WIRING.md). The short version:
`VRx` → **GPIO 36**, `VRy` → **GPIO 39**, `+5V` → **3V3** (not 5 V), `GND` → `GND`,
and leave the joystick's `SW` pin unconnected.

## Build & upload

```
pio run                                  # build
pio run -t upload                         # flash
pio device monitor                        # serial @ 115200
pio run -e joystick-check -t upload       # wiring diagnostic: OLED + stick only
pio device list                           # find the port if upload can't
```

## Controls

| Input | What it does |
|---|---|
| Joystick up / down | Previous / next page |
| Joystick left / right | Adjust the current page's value |
| BOOT button, short press | The current page's action |
| BOOT button, long press | On SETTINGS: transmit self-test (asks first) |

Hold an axis to repeat, the way a keyboard does.

## The pages

| Page | Shows | Left/right | Click |
|---|---|---|---|
| **LIVE** | Frequency, live signal strength, last packet's RSSI/SNR, packet count and rate | Tune | Reset counters |
| **PACKET** | Last payload as text and hex, length, RSSI/SNR, age | Tune | Swap text/hex emphasis |
| **SIGNAL** | Signal strength over time, scrolling right to left | Tune | Clear the graph |
| **SWEEP** | Noise floor across 863–870 MHz, with a movable cursor | Move cursor | Start / stop sweeping |
| **SETTINGS** | Preset, SF, bandwidth, coding rate, sync word, tune step, raw joystick values | Change value | Next row |

The status bar carries battery percent (or `USB`), `SWP` while a sweep is running,
and `RF!` if the radio didn't initialise.

**Sweeping and receiving are mutually exclusive** — the radio has to retune for
every bin, so nothing can be decoded while a sweep runs. The SWEEP page says so
rather than going silently deaf.

## Presets

Dialling six parameters by hand gets old, so SETTINGS has a preset row:

| Preset | Frequency | SF | BW | Sync | Why |
|---|---|---|---|---|---|
| LoRaWAN SF7 | 868.1 MHz | 7 | 125 kHz | `0x34` | Default. Short-range LoRaWAN traffic |
| LoRaWAN SF9 | 868.1 MHz | 9 | 125 kHz | `0x34` | Mid-range |
| LoRaWAN SF12 | 868.1 MHz | 12 | 125 kHz | `0x34` | Slowest, longest reach |
| Mesh LongFast | 869.525 MHz | 11 | 250 kHz | `0x2B` | Meshtastic's EU default |
| Private `0x12` | 868.0 MHz | 7 | 125 kHz | `0x12` | The LoRa library's own default — matches hobby projects, including [`../lora-morse`](../lora-morse) |

Spreading factor 6 is deliberately not offered: it only works in implicit-header
mode, which would quietly stop the receiver decoding anything normal.

## Bringing it up the first time

Do this in order — each step rules out one thing, which matters because you have
only one radio and a quiet band looks identical to a broken receiver.

1. **`pio run -e joystick-check -t upload`.** This build drives only the OLED and
   the joystick, with no radio involved. You should see two live numbers and a
   crosshair that follow the stick, and the last event name changing as you push
   it. Push to each extreme: the numbers should approach 0 and 4095.
2. **`pio run -t upload`, then watch the splash screen.** It reports `SX1276 ok` or
   `SX1276 FAIL`. This is a real test, not a guess — `LoRa.begin()` reads the
   chip's version register, so `FAIL` means SPI wiring or a dead radio, never just
   a quiet band.
3. **Go to SETTINGS and change a few values.** Each one should stick.
4. **Go to SWEEP and click.** The noise floor should look plausible — roughly
   −120 dBm when quiet — and should visibly **rise** when you key any 868 MHz
   device nearby: a wireless doorbell, weather station, or car remote. This is the
   proof that the receive path works end to end, and it doesn't depend on anyone
   transmitting LoRa.
5. **Leave LIVE running** at SF7, then try SF9 and SF12. Nothing appearing is not a
   failure — see the limits at the top.

## Transmit self-test

Long-press BOOT on the SETTINGS page. It asks for confirmation, then sends **one**
short packet at 14 dBm.

What it proves: the transmit path works and the board doesn't brown out. What it
does **not** prove: anything at all about reception — you'd need a second radio for
that. The screen says as much.

On legality: EU rules for 868.0–868.6 MHz allow 25 mW (14 dBm) at a 1 % duty cycle,
so a single short packet is comfortably inside them. Everything else this firmware
does is passive listening.

## Notes

- **Up and down feel backwards?** Axis direction depends on how the joystick module
  is physically oriented. Rotate it 180°, or swap the `Up`/`Down` (or `Left`/`Right`)
  cases in `poll()` in [`src/joystick.cpp`](src/joystick.cpp).
- **Blank screen on power-up?** You were probably holding the BOOT button, which
  puts the ESP32 into flash mode. Let go and press `RST`.
- **`STICK? 3V3+GND` on the splash or the diagnostic build?** The centre reading at
  boot was implausible. GPIO 36/39 are input-only with no pull-ups, so an unwired or
  unpowered stick floats and reads garbage. Check 3V3 and GND. Also: leave the stick
  alone while the board boots — it measures the centre position at startup.
- **Upload can't find the board?** Run `pio device list`. The CP2104 bridge doesn't
  always get the same COM port, which is why no `upload_port` is pinned.
- Every received packet is also logged over serial at 115200 with timestamp, size,
  RSSI, SNR and frequency error.

## Source layout

| File | Responsibility |
|---|---|
| [`src/main.cpp`](src/main.cpp) | Wiring only — pumps the joystick, dispatches events, draws |
| [`src/radio.cpp`](src/radio.cpp) | SX1276 state, reception, statistics, sweep, self-test |
| [`src/ui.cpp`](src/ui.cpp) | Everything drawn on the OLED |
| [`src/joystick.cpp`](src/joystick.cpp) | ADC and button → discrete events |
| [`src/battery.cpp`](src/battery.cpp) | Battery voltage and percent |
| [`include/pins.h`](include/pins.h) | Every pin, one place |
| [`src/diagnostics/JoystickCheck.cpp`](src/diagnostics/JoystickCheck.cpp) | The `joystick-check` build |

Data flows one way: the joystick emits events, `main.cpp` dispatches them, the radio
changes state, and the UI reads the radio and draws. The UI never commands the
radio; the radio never draws.
