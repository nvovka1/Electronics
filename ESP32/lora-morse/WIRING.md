# LoRa Morse — Wiring & Power

LILYGO / TTGO LoRa32 (all-in-one: ESP32 + LoRa radio + OLED on one board).
**Two-way (half-duplex):** both boards run the same firmware and can send *and*
receive. Each listens all the time and transmits on button press / typed text.

> **Golden rule:** every device must share a common **GND** with the board.

## What's already on the board (do NOT wire)
The LoRa radio and OLED are wired internally on the PCB. PlatformIO's
`board = ttgo-lora32-v21` defines their pins automatically:

| Function | Internal pin |
|----------|--------------|
| LoRa SCK / MISO / MOSI | 5 / 19 / 27 |
| LoRa CS / RST / IRQ(DIO0) | 18 / 23 / 26 |
| OLED SDA / SCL | 21 / 22 |
| On-board LED | 25 |
| On-board button ("PRG" / IO0) | 0 |

⚠️ **Attach the antenna before powering** — transmitting without one can kill the radio.
⚠️ Set `LORA_FREQ` in `src/main.cpp` to match your board (433 / 868 / 915 MHz),
identical on both boards.

## What you wire externally

| Component | Component pin | Board pin | Notes |
|-----------|---------------|-----------|-------|
| Active buzzer | + | **GPIO 13** | Morse beep |
| Active buzzer | – | **GND** | |
| **Antenna** | — | u.FL / SMA | required! |

### Send button (optional)
No external button needed — the code uses the **on-board PRG button (GPIO 0)**.
To add your own button instead: wire it between **GPIO 0 and GND** (it uses `INPUT_PULLUP`,
so pressed = LOW).

## How to send — the button is a Morse KEY
- **Hold the button** → LED + buzzer are ON for as long as you press (live sidetone).
- **Short press = dot (·)**, **long press = dash (–)** (threshold `DASH_MIN_MS`).
- **Pause** (`LETTER_GAP_MS`) → the keyed dots/dashes decode into a **letter**, sent over LoRa.
- **Longer pause** (`WORD_GAP_MS`) → a **space** is sent.

Tune the three timing constants in `src/main.cpp` to your hand.

Every board **listens continuously**; each received character is appended to the
screen and beeped as Morse. The **OLED shows**: node name, `TX:` (what you've keyed),
`RX:` (what you've received), and `key:` (the dots/dashes in progress).

## Power / battery — built in ✅
The LILYGO LoRa32 has an **on-board Li-ion charger + regulator**:
- Drop a charged **18650** into the holder (watch **+/–** polarity!), **or** plug a
  **3.7 V LiPo** into the JST connector.
- **Charge it over USB** — connecting USB charges the cell.
- No boost converter needed; the board handles it.

| Use | Best power |
|-----|-----------|
| Testing / programming | USB cable |
| Portable | 18650 in holder, or LiPo on JST (charges via USB) |

## Build & upload (two named nodes, same firmware)
```
pio run -e boardA -t upload    # board #1 -> name "A"
pio run -e boardB -t upload    # board #2 -> name "B"
pio device monitor             # serial @ 115200
```
If your board is an older revision, change `board` in `platformio.ini` to
`ttgo-lora32-v2` or `ttgo-lora32-v1` (printed on the PCB).
