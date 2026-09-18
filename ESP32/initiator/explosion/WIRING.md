# Wiring — explosion node

Board: **LILYGO/TTGO LoRa32 v2.1** ("T3_V1.6"). The SX1276 radio and the SSD1306
OLED are on the board already and need no wiring; `board = ttgo-lora32-v21`
defines their pins.

## What you add: four LEDs

One LED per state, and exactly one is lit at any moment. The colours match the
badges on the dashboard on purpose — the person at the board and the person at
the screen should describe what they see in the same words.

| State | Colour | Board pin | Resistor | Wiring |
|---|---|---|---|---|
| SAFE | blue | **GPIO 13** | 330 Ω | anode → pin, cathode → GND |
| INIT | green | **GPIO 2** | 220 Ω | anode → pin, cathode → GND |
| ARMED | yellow | **GPIO 14** | 220 Ω | anode → pin, cathode → GND |
| FIRE | red | **GPIO 0** | 220 Ω | **anode → 3.3 V, cathode → pin** |

Three are wired the ordinary way: anode (long leg) to the pin through the
resistor, cathode (short leg) to GND.

**The red one is wired backwards, and it has to be** — anode to **3.3 V**,
cathode through the resistor to GPIO 0. With a LED to GND on that pin the board
does not run at all; see *GPIO 0* below. The firmware knows: `LedFireActiveLow`
in [src/board_pins.h](src/board_pins.h) is what tells it, and it drives that pin
LOW to light the LED.

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

**GPIO 34–39** are **input-only**. There is no output driver in the silicon, so
`pinMode(OUTPUT)` is a silent no-op and `digitalWrite` does nothing. A LED on
one of these never lights and nothing anywhere reports a fault. GPIO 35 is also
the battery divider.

**GPIO 32 and 33** are the 32.768 kHz crystal on this board revision and are not
broken out at all — if you cannot find them on the silkscreen, that is why.

## Pins used here that are worth knowing about

**GPIO 0 is the boot strapping pin, and that is why the red LED is backwards.**

With an ordinary LED to GND the board never runs. The on-board 10 kΩ pull-up can
only source about 330 µA, the LED clamps the pin near 1.6 V, and the ESP32 needs
above 2.48 V to read high at reset. So GPIO 0 is LOW when reset is released and
the chip comes up in **serial download mode** instead of starting the firmware —
no radio, no ACK, and a board that looks perfectly alive.

Flashing still appears to work in that state, because the board is already in
the mode the flasher wants. A successful upload therefore does **not** prove the
pin is wired correctly. The test is that the board boots: the OLED draws and the
blue SAFE LED comes on within a second of reset.

Wired anode-to-3.3 V the LED becomes a pull-up instead, the pin reads HIGH at
reset, and the board boots normally.

One cost: GPIO 0 is also the on-board PRG button, which shorts it to GND.
Pressing PRG while the pin is driven HIGH (red off) shorts the output. It
survives a brief press, but it is a reason not to press PRG while the node is
running.

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
