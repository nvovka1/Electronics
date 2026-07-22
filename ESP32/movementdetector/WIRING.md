# Movement Detector — Wiring & Power

ESP32 DevKit + PIR motion sensor + LED + buzzer + I²C OLED (+ optional relay).

> **Golden rule:** every device must share a common **GND** with the ESP32.

## Wiring

| Component | Component pin | ESP32 pin | Notes |
|-----------|---------------|-----------|-------|
| PIR HC-SR501 | VCC | **5V / VIN** | PIR needs ~5 V |
| PIR HC-SR501 | OUT | **GPIO 13** | 3.3 V signal, safe |
| PIR HC-SR501 | GND | **GND** | |
| LED | anode (+, long leg) | **GPIO 4** → 220–330 Ω resistor | resistor in series |
| LED | cathode (–, short) | **GND** | |
| Active buzzer | + | **GPIO 5** | sounds on motion |
| Active buzzer | – | **GND** | |
| OLED SSD1306 | VCC | **3.3V** | 3.3 V, NOT 5 V |
| OLED SSD1306 | GND | **GND** | |
| OLED SSD1306 | SDA | **GPIO 21** | I²C data |
| OLED SSD1306 | SCL | **GPIO 22** | I²C clock |

### Optional 2-channel relay (to switch a real lamp)
| Relay pin | ESP32 pin | Notes |
|-----------|-----------|-------|
| VCC | **5V / VIN** | relay coil needs 5 V |
| GND | **GND** | |
| IN1 | **GPIO 25** | active-LOW on most modules |
| IN2 | **GPIO 26** | second channel (optional) |

Relay output (per channel): **middle screw = COM**, one outer = **NO**, other = **NC**.
Lamp on with motion: `power(+) → COM`, `NO → lamp(+)`, `lamp(–) → power(–)`.
⚠️ Mains 220 V is lethal — practice with a low-voltage (5–12 V) lamp first.

## Power / battery

- **Easiest:** a 5 V **USB power bank** into the USB port (also powers the PIR via VIN). No extra parts.
- **VIN pin:** feed regulated **5 V** here (ideal for a stable 3.3 V rail).
- **Li-ion 18650 / LiPo:** do **not** wire a raw 3.7 V cell to the board — too low for VIN,
  too high for the 3V3 pin. Use **18650/LiPo → boost converter to 5 V (e.g. MT3608) → VIN**,
  optionally with a **TP4056** charge board.
- **4× AA (6 V) → VIN** works; **9 V block → VIN** works but wastes energy as heat; **3× AA (4.5 V)** is too marginal.

| Use | Best power |
|-----|-----------|
| Desk / testing | USB cable or USB power bank |
| Portable | 18650/LiPo + boost-to-5V → VIN |

## Build & upload
```
pio run -t upload      # flash
pio device monitor     # serial @ 115200
```
