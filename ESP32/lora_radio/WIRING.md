# LoRa Band Scanner — Wiring & Power

A LILYGO T3 v1.6.1 (868 MHz) plus one analog joystick. Four wires, no resistors,
no breadboard needed.

> **Golden rule:** the joystick's power pin goes to **3V3**, never 5V. The ESP32's
> ADC inputs are destroyed by anything above ~3.6 V, and a joystick fed from 5 V
> puts up to 5 V straight onto GPIO 36 and 39.

## What's already on the board (do NOT wire)

Everything below is soldered on the T3 already. It is here so you know which pins
are unavailable, not so you can connect to them.

| Function | Internal pin |
|---|---|
| LoRa SCK | GPIO 5 |
| LoRa MISO | GPIO 19 |
| LoRa MOSI | GPIO 27 |
| LoRa CS | GPIO 18 |
| LoRa RST | GPIO 23 |
| LoRa DIO0 | GPIO 26 |
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| OLED RST | GPIO 16 |
| Green LED | GPIO 25 |
| BOOT button | GPIO 0 |
| Battery sense (÷2) | GPIO 35 |
| microSD slot (unused) | GPIO 2, 13, 14, 15 |

This board has almost no spare I/O left. **GPIO 34, 36 and 39 are the only free
pins**, and all three are input-only with no internal pull-ups — which is exactly
why the design uses two of them for the analog axes and takes "click" from the
BOOT button instead.

## What you wire externally

A KY-023 (or any 2-axis thumbstick module with `VRx` / `VRy` / `SW` / `+5V` / `GND`).

| Component | Component pin | Board pin | Notes |
|---|---|---|---|
| Joystick | `VRx` | **GPIO 36** | X axis — left/right |
| Joystick | `VRy` | **GPIO 39** | Y axis — up/down |
| Joystick | `+5V` | **3V3** | ⚠️ 3V3, *not* 5V — see the golden rule |
| Joystick | `GND` | **GND** | |
| Joystick | `SW` | *nothing* | Leave it unconnected |

On the T3's headers, `36` and `39` sit on the same side as `34`, `35` and `RST`;
`3V3` and a `GND` are on the opposite header. Both headers carry a `GND`, so use
whichever is closer.

### Why `SW` is left unconnected

The joystick's button shorts `SW` to GND when pressed and floats otherwise, so it
needs a pull-up. The only pin left for it would be GPIO 34, which has no internal
pull-up, so it would need an external 10 kΩ resistor to 3V3. The BOOT button on
the board does the same job for free — so **BOOT is the "click" button** in this
firmware.

If you would rather have everything on the stick, add the 10 kΩ from GPIO 34 to
3V3, wire `SW` to GPIO 34, and read it alongside `BUTTON_PIN` in
[`src/joystick.cpp`](src/joystick.cpp).

### ⚠️ Antenna

**Attach the antenna before you power the board up, and certainly before running
the transmit self-test.** Driving the power amplifier into an open circuit can
damage it. Receiving without an antenna is harmless but nearly deaf.

The supplied spring/wire antenna works. A proper 868 MHz SMA whip is the single
biggest improvement you can make to what this thing can hear.

## The BOOT button caveat

GPIO 0 is the ESP32's download-mode strap. Pressing it while the firmware runs is
completely safe — that's what this project uses it for. But **do not hold it while
the board powers up or resets**, or the ESP32 enters flash mode and the screen
stays dark. If your display is blank after plugging in, let go of the button and
press `RST`.

## Power / battery — built in ✅

| Use | Best power |
|---|---|
| At the desk | micro-USB |
| Portable / walking around | 18650 cell in the on-board holder |

The T3 v1.6.1 has an 18650 holder and a charge circuit on the underside, so a cell
charges whenever USB is connected. The status bar shows battery percent, or `USB`
when the sensed voltage is high enough that it is clearly running from the cable.

Mind the cell polarity marking in the holder — it is silkscreened, and reversing a
Li-ion cell is a genuinely bad afternoon.

## Build & upload

```
pio run                                  # build the scanner
pio run -t upload                         # flash it
pio device monitor                        # serial @ 115200
pio run -e joystick-check -t upload       # wiring diagnostic: OLED + stick only
pio device list                           # find the port if upload can't
```

No `upload_port` is pinned in `platformio.ini` on purpose. This board's CP2104
USB-serial bridge does not always land on the same COM port, and a hardcoded port
is the thing that silently breaks later.
