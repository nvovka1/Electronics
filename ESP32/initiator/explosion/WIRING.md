# Wiring — explosion node

Board: **LILYGO/TTGO LoRa32 v2.1** ("T3_V1.6"). The SX1276 radio and the SSD1306
OLED are on the board already and need no wiring; `board = ttgo-lora32-v21`
defines their pins.

## What you add: four LEDs

One LED per state, and exactly one is lit at any moment. The colours match the
badges on the dashboard on purpose — the person at the board and the person at
the screen should describe what they see in the same words.

| State | Colour | Board pin | Series resistor |
|---|---|---|---|
| SAFE | blue | **GPIO 32** | 330 Ω |
| INIT | green | **GPIO 33** | 220 Ω |
| ARMED | yellow | **GPIO 2** | 220 Ω |
| FIRE | red | **GPIO 14** | 220 Ω |

Anode (long leg) to the pin through the resistor, cathode (short leg) to **GND**.

The resistor values assume ordinary 20 mA indicator LEDs at 3.3 V. Red, yellow
and green drop about 2 V, so 220 Ω gives roughly 6 mA — bright enough indoors
and comfortably inside the ESP32's 40 mA per-pin limit. Blue drops about 3 V, so
it wants the larger resistor to land in the same place.

## Pins you must not use

**GPIO 16.** On these boards the ESP32-PICO-D4 uses it for the embedded flash
chip select. Driving it hangs the board in a silent watchdog reboot loop with
nothing on the serial line to say why, and the board looks bricked. It is not
used anywhere in this firmware and must not be.

**GPIO 6–11.** SPI flash.

**GPIO 34–39** are input-only, so they cannot drive a LED. GPIO 35 is already
the battery divider.

## Pins used here that are worth knowing about

**GPIO 2 is a strapping pin.** It must not be held HIGH while the board boots. A
LED to GND is the safe direction: it presents no pull-up, and the pin stays an
input until `setup()` drives it. Do not put a pull-up on this pin.

**GPIO 2 and GPIO 14 are routed to the microSD socket.** Nothing here uses a
card, so they are free. If you ever populate the slot, these two LEDs move.

## Antenna

**Attach the antenna before powering the board.** Transmitting into an open
connector reflects the power back into the SX1276's output stage, and enough of
that kills the radio. The node transmits on its own — an ACK for every command
and a repeated announcement when it arms itself — so there is no "as long as I
don't press anything" here.

## Power

USB is fine on a bench. On a battery, the 18650 holder on the underside feeds
the same regulator; GPIO 35 reads it through a 100k/100k divider, and the
firmware reports tenths of a volt.

A battery reading of **zero is not a flat battery** — it is the node saying its
own ADC reading failed and must not be believed. The dashboard shows it as
"untrusted" rather than as empty.
