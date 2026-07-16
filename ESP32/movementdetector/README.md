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

### Pin summary

- **GPIO 13** → PIR OUT (input)
- **GPIO 4**  → LED (output)
- **5V/VIN**  → PIR VCC
- **GND**     → PIR GND and LED cathode (share ground)

> Want to use the on-board LED instead of an external one? Change `LED_PIN` in
> `src/main.cpp` to `2` (GPIO2 drives the blue on-board LED on most dev kits) and
> skip the external LED + resistor.

## Build & upload

```
pio run                 # build
pio run --target upload # flash to the board
pio device monitor      # open serial monitor @ 115200 baud
```

## Notes

- The HC-SR501 needs ~30–60 s to warm up after power-on; ignore readings during
  that time.
- The sensor has two trim pots: **sensitivity** (detection distance) and **time
  delay** (how long OUT stays HIGH). Adjust as needed.
- Set the sensor's jumper to **retriggering (H)** mode so continuous motion keeps
  the output HIGH.
