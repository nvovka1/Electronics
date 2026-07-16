# ESP32 Movement Detector

A PIR motion sensor turns an LED **ON** when movement is detected and **OFF**
after ~2 s of no movement.

## PlatformIO board

In `platformio.ini` the board is set to **`esp32dev`**
(`platform = espressif32`, `framework = arduino`). This is the generic
"ESP32 Dev Kit" / "ESP32 DevKitC" (the common WROOM-32 dev board). It works for
almost every generic 30/38-pin ESP32 dev kit.

> If you have a different variant, pick its board id instead, e.g.
> `esp32doit-devkit-v1`, `nodemcu-32s`, `esp32-c3-devkitm-1`. Run
> `pio boards esp32` to list all options.

## Hardware

- ESP32 Dev Kit board
- PIR motion sensor — HC-SR501 (the typical "dev kit" movement detector)
- 1x LED
- 1x 220–330 Ω resistor (current limiting for the LED)
- Active buzzer (optional)
- 0.96" I²C OLED display — SSD1306, 128×64
- Breadboard + jumper wires

## Wiring

### PIR sensor (HC-SR501) — 3 pins

| PIR pin | Connect to ESP32 |
|---------|------------------|
| VCC     | **5V / VIN**     |
| GND     | **GND**          |
| OUT     | **GPIO 13**      |

The HC-SR501 needs 5 V on VCC, but its OUT signal is 3.3 V — safe for the ESP32.

### LED

| LED leg            | Connect to |
|--------------------|------------|
| Anode (+, long leg)| **GPIO 4** → through 220–330 Ω resistor |
| Cathode (–, short) | **GND**     |

So the chain is: `GPIO 4 → resistor → LED(+) → LED(-) → GND`.

```
 ESP32 GPIO4 ──[220Ω]──►|── GND
                        LED
```

### OLED display (SSD1306, I²C) — 4 pins

| OLED pin | Connect to ESP32 |
|----------|------------------|
| VCC      | **3.3V** ⚠️ (see note) |
| GND      | **GND**          |
| SDA      | **GPIO 21**      |
| SCL      | **GPIO 22**      |

> ⚠️ **Power the OLED from 3.3V, not VIN/5V.** The ESP32 drives SDA/SCL at
> 3.3 V. If the panel is powered at 5 V its I²C logic-HIGH threshold rises to
> ~4 V, so the ESP32's 3.3 V signals may never be recognized and **the screen
> stays black** even though it looks powered. This is the #1 cause of a dead
> SSD1306 on an ESP32.

### Pin summary

- **GPIO 13** → PIR OUT (input)
- **GPIO 4**  → LED (output)
- **GPIO 5**  → active buzzer (+)
- **GPIO 21** → OLED SDA
- **GPIO 22** → OLED SCL
- **5V/VIN**  → PIR VCC
- **3.3V**    → OLED VCC
- **GND**     → PIR GND, LED cathode, buzzer (–), OLED GND (share ground)

> Want to use the on-board LED instead of an external one? Change `LED_PIN` in
> `src/main.cpp` to `2` (GPIO2 drives the blue on-board LED on most dev kits) and
> skip the external LED + resistor.

## Build & upload

```
pio run                 # build
pio run --target upload # flash to the board
pio device monitor      # open serial monitor @ 115200 baud
```

## Serial monitor

The firmware talks over USB serial at **115200 baud** (`Serial.begin(115200)`).
Once connected you should see `ESP32 Movement Detector ready.` at boot, then
`Motion detected -> ON` / `No motion -> OFF` as the sensor triggers.

Open it with either:

```
pio device monitor            # uses the settings in platformio.ini
pio run -t upload -t monitor  # flash, then immediately open the monitor
```

The monitor settings live in `platformio.ini` so a bare `pio device monitor`
"just works":

| Setting            | Value                                    | Why |
|--------------------|------------------------------------------|-----|
| `monitor_port`     | `COM4`                                    | Pins the monitor to the board's port (same as `upload_port`). |
| `monitor_speed`    | `115200`                                  | Must match `Serial.begin(115200)`. The CLI default is **9600**, which shows garbage. |
| `monitor_filters`  | `esp32_exception_decoder, default, time` | Decodes crash backtraces to `file:line`; `time` timestamps each line. |

### If the monitor shows nothing / garbage / keeps disconnecting

Work through these in order — the first two fix the vast majority of cases:

1. **Wrong baud.** A blank line then random symbols = baud mismatch. Running
   `pio device monitor` *without* config defaults to 9600. Always keep
   `monitor_speed = 115200` in `platformio.ini`, or pass `-b 115200`.
2. **A burst of garbage at boot is normal.** The ESP32 ROM bootloader logs at a
   scaled baud on 26 MHz-crystal boards, so you see junk once per reset, then
   clean text. If the monitor *stays* blank or resets in a loop, and you don't
   want the monitor toggling the board on connect, add `monitor_rts = 0` and
   `monitor_dtr = 0` to `platformio.ini`.
3. **No / wrong COM port.** Run `pio device list`.
   - If **nothing** is listed, the board isn't plugged in (or the USB-serial
     chip has no driver). This board uses a **CH340**
     ([driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html)); other kits
     use a [CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).
   - If **several** ports are listed, pick the right chip. This ESP32 shows up
     as **`USB-SERIAL CH340`** (`VID_1A86`, port **COM3** on this PC). Do **not**
     use an unrelated **FTDI** `USB Serial Port` (`VID_0403`, COM4 here) — that
     is a different device and reads black. Set `upload_port`/`monitor_port`
     accordingly.
   - Port numbers can change between reboots/USB slots — re-check with
     `pio device list` if the monitor suddenly goes black.
4. **Port already open.** Only one program can hold the port. Close any other
   serial monitor (Arduino IDE, a second PlatformIO monitor, PuTTY) first —
   symptom is "could not open port ... Access is denied".
5. **A bad USB cable.** Charge-only cables enumerate no port at all. Use a
   known data cable.

To quit the monitor press **Ctrl+C**.

## OLED display not working (screen stays black)

If the screen stays black, `setup()` prints `SSD1306 not found - check
wiring/address.` on the serial monitor. Work through these — in order of how
often they're the culprit:

1. **Brownout / reset loop.** If the serial log shows `Brownout detector was
   triggered` or the board keeps rebooting, it's a power problem — use a good
   **data** USB cable in a **direct** port (no hub), or feed a solid 5 V into
   VIN. The board can't drive the display while it's resetting.
2. **VCC on VIN/5V instead of 3.3V** — move it (see the ⚠️ note above).
3. **SDA/SCL swapped** — SDA→GPIO 21, SCL→GPIO 22.
4. **Loose jumper / bad GND** — reseat all four wires.
5. **Wrong address** — the code tries `0x3C` then `0x3D` automatically; if your
   module is something else, set `OLED_ADDR` in
   [`src/main.cpp`](src/main.cpp).
6. **Not actually an SSD1306** — a look-alike **SH1106** shows black/garbage
   with this driver and needs a different one (see below).
7. **Dead module** — try a known-good one.

### Trying a different display library

`Adafruit_SSD1306` (used here) needs a correct address and an SSD1306
controller. If you have an SH1106 or want a more forgiving driver,
**[U8g2](https://github.com/olikraus/u8g2)** supports many controllers and both
I²C/SPI. Add it in `platformio.ini`:

```
lib_deps =
    olikraus/U8g2 @ ^2.35.30
```

But a different library **won't help a wiring/power fault** — fix a black bus
(brownout, VCC, SDA/SCL) first, then a library only matters if the controller
itself is different.

## Notes

- The HC-SR501 needs ~30–60 s to warm up after power-on; ignore readings during
  that time.
- The sensor has two trim pots: **sensitivity** (detection distance) and **time
  delay** (how long OUT stays HIGH). Adjust as needed.
- Set the sensor's jumper to **retriggering (H)** mode so continuous motion keeps
  the output HIGH.
