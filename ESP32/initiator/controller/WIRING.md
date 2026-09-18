# Wiring — controller

Board: **LILYGO/TTGO LoRa32 v2.1** ("T3_V1.6"), the same board as the explosion
node. The SX1276 radio and the SSD1306 OLED are on the board already and need no
wiring.

## What you add: two buttons

The third is already there — the on-board **PRG** button is the sequence button.

| Button | Board pin | Job | Extra part |
|---|---|---|---|
| SEQ | **GPIO 0** (on-board PRG) | advance INIT → ARM → FIRE | none |
| SAFE | **GPIO 13** | send SAFE, always | none |
| TARGET | **GPIO 15** | cycle which node is commanded | none |

Wire each added button **between the pin and GND**.

All three need nothing else. Each is configured `INPUT_PULLUP`, so the internal
pull-up holds the pin high and the button pulls it low. Do not add a pull-up of
your own — two pull-ups fighting is a button that reads as half-pressed.

A momentary push-to-make switch of any size. Debounce is in software
(`ButtonDebounceMs`, 40 ms), so no capacitor is needed.

### Why TARGET is not on GPIO 36

It was, and it cost hours. Worth reading before moving any button onto 34–39.

Those pins have **no internal pull-up at all** — `INPUT_PULLUP` is silently
ignored and the pin floats. Floating, it invented presses. TARGET walked the aim
from node 1 to node 4 and wrote that to NVS. Every command after that went to a
node that does not exist; the real node heard each one, saw it was addressed
elsewhere and stayed silent, exactly as designed. From the controller it looked
like a dead radio link — `tx_NO_ACK`, three attempts, nothing. It survived
reflashing, because the wrong target was in flash rather than in the image.

An external 10 kΩ to 3.3 V does make those pins usable. GPIO 15 needs no
resistor, which is a better answer than remembering one.

The firmware now logs `NO_PULLUP  GPIO nn reads low at boot` if a pin without an
internal pull-up looks unconnected, and `status` prints the target on its own
line — that is the first number to check whenever it transmits and nothing
answers.

### Which button goes where matters

SAFE is the one control that works from any state, and it is worth it being the
one your thumb finds without looking. Put it somewhere distinct — a different
colour, a different size, or physically separated from the other two. The
firmware does not care; the person holding it at the wrong moment does.

## Pins you must not use

**GPIO 32 and 33** — the 32.768 kHz crystal on this board revision, and not
broken out at all. If you cannot find them on the silkscreen, that is why.

**GPIO 34–39** — these read inputs, but they have **no internal pull-up**.
`INPUT_PULLUP` is silently ignored, the pin floats, and the button reads noise:
usually stuck, sometimes phantom presses. One of them can be used with an
external 10 kΩ resistor to 3.3 V; GPIO 13 and 14 need nothing, which is why they
are the ones here.

**GPIO 16** — embedded flash chip select on the PICO-D4. Driving it hangs the
board in a silent watchdog reboot loop and it looks bricked.

**GPIO 6–11** — SPI flash.

**GPIO 34–39** are input-only. They would work as buttons, but GPIO 35 is
already the battery divider.

## One thing about GPIO 0

It is a strapping pin: held LOW while the board resets, the ESP32 enters the
bootloader instead of running. That is exactly what the PRG button is for, so it
is not a fault — but **holding SEQ while power-cycling gives you a board that
looks dead** and is actually sitting in the bootloader waiting to be flashed.
Let go and press reset.

If that catches people out often enough to matter, move SEQ to GPIO 25 and
change `ButtonSequencePin` in [src/board_pins.h](src/board_pins.h). That pin is
free on this board — the node uses it for nothing and neither does this.

## Antenna

**Attach the antenna before powering the board.** This board transmits every
time a button is pressed. Transmitting into an open connector reflects the power
back into the SX1276's output stage, and enough of that kills the radio.

## Power

USB on a bench; the 18650 holder on the underside in the field. This board does
not run WiFi, so it draws considerably less than the node — most of its time is
spent idle in `vTaskDelay`, waiting for a button.
