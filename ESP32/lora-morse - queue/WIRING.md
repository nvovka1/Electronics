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

## How to send — single tap = dot, double tap = dash
- **One tap** → a **dot** (`.`) is transmitted.
- **Two quick taps** (second one within `DOUBLE_GAP_MS`) → a **dash** (`-`) is transmitted.
- Holding the button lights the LED + buzzer as live feedback (sidetone).

Tune `DEBOUNCE_MS` and `DOUBLE_GAP_MS` in `src/main.cpp` to your hand.

Every board **listens continuously**; each received symbol is appended to the
screen and beeped. The **OLED shows**: node name, `TX:` (what you've sent),
`RX:` (what you've received), and a status line with the last event.

## Firmware structure — FreeRTOS tasks + queues
The button, the dispatch logic and the radio run as independent units that only
exchange events through queues:

```
button -> Button Task -> keyEventQueue -> loop() -> radioQueue -> Radio Task -> LoRa
                                                                       |
                                                                    uiQueue -> UI Task -> OLED
```

| Unit | Priority / core | Owns | Responsibility |
|------|-----------------|------|----------------|
| `buttonTask` | 3 / core 1 | key GPIO 0 | debounce, classify single vs. double press, post `KeyEvent` |
| `radioTask` | 2 / core 0 | LoRa radio | transmit queued symbols, poll for incoming ones, post `UiEvent` |
| `loop()` | 1 / core 1 | — | block on `keyEventQueue`, map press → `.`/`-`, post `RadioRequest` |
| `uiTask` | 1 / core 0 | OLED, I²C, TX/RX text | apply the event to the TX/RX lines and redraw |

One module per task, each owning its queue, its hardware and its own tunables:

| File | Contains | Tunables |
|------|----------|----------|
| [`main.cpp`](src/main.cpp) | `setup()` starts the tasks, `loop()` dispatches | task priorities and cores |
| [`button_task.cpp`](src/button_task.cpp) | debounce + single/double classification | `DEBOUNCE_MS`, `DOUBLE_GAP_MS`, `BUTTON_POLL_MS` |
| [`radio_task.cpp`](src/radio_task.cpp) | LoRa transmit + receive polling | `RADIO_POLL_MS` |
| [`ui_task.cpp`](src/ui_task.cpp) | OLED drawing, TX/RX text lines | `RX_CLEAR_AFTER` |
| [`tone.cpp`](src/tone.cpp) | LED + buzzer, the one shared device | `DOT_MS`, `DASH_MS` |
| [`app_events.h`](src/app_events.h) | the structs that travel on the queues | — |
| [`board_pins.h`](src/board_pins.h) | pins and `LORA_FREQ` | all pin numbers |

- Three queues, 8 slots each, one per module — each is `static`, reachable only
  through its module's functions (`radioSendSymbol`, `uiPostSymbolSent`,
  `buttonWaitForPress`, …), so no task can bypass another's interface.
- Nothing busy-waits: `loop()` and the UI task block on `portMAX_DELAY`, the
  radio task blocks on its queue with a 20 ms timeout (which also paces receive
  polling), and the button task sleeps `BUTTON_POLL_MS` between samples.
- Producers post to `uiQueue` with a 0-tick timeout — a slow ~30 ms OLED redraw
  can never stall the radio or the key. A full queue drops a frame; the next
  event redraws the whole screen anyway.
- Each piece of hardware has exactly one owner (key pin → button task, radio →
  radio task, OLED/I²C → UI task), so no SPI/I²C locking is needed. The only
  shared hardware is the LED + buzzer, guarded by `toneMutex` taken with a
  0 timeout — feedback is skipped rather than blocking.

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
