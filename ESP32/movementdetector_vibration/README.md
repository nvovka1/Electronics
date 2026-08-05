# ESP32 Movement Detector

A PIR motion sensor turns an LED **ON** when movement is detected and **OFF**
after ~2 s of no movement. Each time motion starts, a vibration motor buzzes
out the rhythm of the **Imperial March** opening phrase once (~7 s).

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
- 1x vibration motor **module** (see below)
- SSD1306 128x64 I²C OLED
- Breadboard + jumper wires

### Use a module, not a bare motor

A bare coin / pager (ERM) motor must **not** go straight onto a GPIO. It pulls
roughly 60–100 mA where an ESP32 pin is rated for ~12–20 mA, and being a coil,
it kicks back a voltage spike every time the current is cut. Either one can
kill the pin.

A **vibration motor module** is the same motor on a small breakout that already
carries the driver transistor and a flyback diode. Its `IN` / `S` pin is a plain
logic input, so the ESP32 only has to switch a signal, never carry the motor
current.

Driving a bare motor yourself works too — NPN transistor (or logic-level
MOSFET) on the low side, base resistor from the GPIO, and a flyback diode
across the motor — but the module gives you all of that pre-wired.

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

### Vibration motor module

| Module pin | Connect to |
|------------|------------|
| `VCC` / `+`  | **3.3V** (5 V is fine on most modules) |
| `IN` / `S`   | **GPIO 5** |
| `GND` / `-`  | **GND**    |

The GPIO only switches the module's logic input — the motor current flows from
VCC through the module's own transistor, never through the ESP32 pin.

### Pin summary

- **GPIO 13** → PIR OUT (input)
- **GPIO 4**  → LED (output)
- **GPIO 5**  → vibration motor module IN (output)
- **GPIO 21 / 22** → OLED SDA / SCL
- **5V/VIN**  → PIR VCC
- **GND**     → PIR GND, LED cathode, motor module GND (share ground)

> Want to use the on-board LED instead of an external one? Change `LED_PIN` in
> `src/main.cpp` to `2` (GPIO2 drives the blue on-board LED on most dev kits) and
> skip the external LED + resistor.

## Build & upload

```
pio run                 # build
pio run --target upload # flash to the board
pio device monitor      # open serial monitor @ 115200 baud
```

## The vibration pattern

[`src/vibration.cpp`](src/vibration.cpp) holds a `{vibrates, durationMs}` table
and a small `millis()`-based sequencer stepped from `loop()` — nothing blocks, so
the PIR and the OLED keep working while the pattern runs. To change it, edit the
`PATTERN[]` table; `false` means a still slot.

A motor has no pitch, only on and off, so what survives of the Imperial March is
its rhythm — the slot durations are the same ones the melody used.

Each slot ends in a still gap so consecutive pulses stay distinct. The gap is
30% of the slot but never less than 60 ms (`GAP_PERCENT` / `MIN_GAP_MS`): an ERM
motor coasts for a few tens of ms after the current is cut, so a purely
proportional gap would let the short 150 ms pulses smear into one long buzz.

Behaviour details:

- One full play-through per OFF→ON transition. Continuous motion does **not**
  restart it.
- Once started, the pattern runs to the end. Because the motion hold is only 2 s,
  the motor can still be buzzing after the display reads `OFF`. Call
  `vibrationStop()` in the OFF branch of `loop()` if you'd rather cut it short.

## Notes

- The HC-SR501 needs ~30–60 s to warm up after power-on; ignore readings during
  that time.
- The sensor has two trim pots: **sensitivity** (detection distance) and **time
  delay** (how long OUT stays HIGH). Adjust as needed.
- Set the sensor's jumper to **retriggering (H)** mode so continuous motion keeps
  the output HIGH.
