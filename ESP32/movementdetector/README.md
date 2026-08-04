# ESP32 Movement Detector

A PIR motion sensor turns an LED **ON** when movement is detected and **OFF**
after ~2 s of no movement. Each time motion starts, the buzzer plays the
opening phrase of the **Imperial March** once (~7 s).

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
- 1x **passive** buzzer / piezo element (see below)
- SSD1306 128x64 I²C OLED
- Breadboard + jumper wires

### Passive vs. active buzzer

The melody needs a **passive** buzzer, because the ESP32 has to generate the
pitch itself. An **active** buzzer has its own oscillator inside — feed it the
same signal and you get one fixed beep, not a tune.

How to tell them apart:

| | Passive (needed) | Active (won't work) |
|---|---|---|
| Sound from a 3.3 V DC coin-cell touch | faint click only | continuous loud tone |
| Underside of a bare disc | open PCB / visible coil | sealed with black epoxy |
| Module label | "passive", sometimes taller body | "active" |
| Resistance across the pins | ~8–16 Ω | hundreds of Ω or more |

If you only have an active buzzer, the code still runs — you'll just hear the
march's rhythm as identical beeps.

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

### Buzzer (passive / piezo)

| Buzzer pin | Connect to |
|------------|------------|
| `+` / signal | **GPIO 5** |
| `-` / GND    | **GND**    |

Driven by the ESP32's LEDC hardware PWM, so **never** `digitalWrite()` GPIO 5 —
the two would fight over the pin. A piezo draws little enough current to sit
directly on the GPIO; a louder magnetic buzzer wants a transistor driver.

### Pin summary

- **GPIO 13** → PIR OUT (input)
- **GPIO 4**  → LED (output)
- **GPIO 5**  → passive buzzer (PWM output)
- **GPIO 21 / 22** → OLED SDA / SCL
- **5V/VIN**  → PIR VCC
- **GND**     → PIR GND, LED cathode, buzzer `-` (share ground)

> Want to use the on-board LED instead of an external one? Change `LED_PIN` in
> `src/main.cpp` to `2` (GPIO2 drives the blue on-board LED on most dev kits) and
> skip the external LED + resistor.

## Build & upload

```
pio run                 # build
pio run --target upload # flash to the board
pio device monitor      # open serial monitor @ 115200 baud
```

## The melody

[`src/melody.cpp`](src/melody.cpp) holds a `{frequencyHz, durationMs}` table and
a small `millis()`-based sequencer stepped from `loop()` — nothing blocks, so the
PIR and the OLED keep working while the march plays. To change the tune, edit
the `MELODY[]` table; `0` Hz means a rest.

Behaviour details:

- One full play-through per OFF→ON transition. Continuous motion does **not**
  restart it.
- Once started, the march plays to the end. Because the motion hold is only 2 s,
  the buzzer can still be playing after the display reads `OFF`. Call
  `melodyStop()` in the OFF branch of `loop()` if you'd rather cut it short.

## Notes

- The HC-SR501 needs ~30–60 s to warm up after power-on; ignore readings during
  that time.
- The sensor has two trim pots: **sensitivity** (detection distance) and **time
  delay** (how long OUT stays HIGH). Adjust as needed.
- Set the sensor's jumper to **retriggering (H)** mode so continuous motion keeps
  the output HIGH.
