# Wiring — ESP32 + ULN2003 + 28BYJ-48 + joystick

Everything runs from **USB 5 V**, except the joystick, which takes **3.3 V**.
No battery, no relay, no capacitor, no current limit to set.

## 1. Motor to driver board

Push the motor's white 5-pin plug into the white socket on the ULN2003 board.

It is **keyed** — it only goes in one way, and there is no coil pairing to work
out. This is the whole reason this motor is easier than a bipolar one: the four
wires you would otherwise have to identify with a multimeter are already sorted
out inside the connector.

## 2. Driver board to ESP32

| ULN2003 | ESP32 | Note |
|---|---|---|
| `IN1` | GPIO **18** | |
| `IN2` | GPIO **19** | |
| `IN3` | GPIO **21** | |
| `IN4` | GPIO **22** | |
| `+` / `5V` (screw terminal or pin) | **VIN** / **5V** | **Not `3V3`** — the coils need 5 V |
| `-` / `GND` | **GND** | |

> **The silkscreen on these boards cannot be trusted.** On this build the wiring
> that actually works is the opposite of what the `+`/`-` markings suggest.
> Clone boards are printed inconsistently, and the labels often only read
> correctly from one orientation.
>
> Go by behaviour, not by the printing. **Correct** means the motor turns and
> the chip stays cool. **Reversed** means the chip gets hot within seconds and
> not one LED lights — see below.

The order of `IN1`..`IN4` matters. Swapping two of them scrambles the coil
sequence, and the motor will buzz or jitter in place instead of turning.

## 3. Joystick

| Joystick | ESP32 | Note |
|---|---|---|
| `VRx` / `X` | GPIO **34** | Analog input |
| `+5V` / `VCC` | **`3V3`** | **Never `5V`** — see below |
| `GND` | **GND** | |
| `VRy` / `Y` | *not connected* | Unused |
| `SW` | *not connected* | Unused |

Three wires. Only the X axis is read.

> **Power the joystick from `3V3`, not `5V`.** Its axes are potentiometers
> wired straight across whatever it is fed, so on 5 V the X output would swing
> to 5 V — and the ESP32's ADC inputs are rated 3.3 V maximum. Feeding 5 V into
> GPIO 34 damages the pin. This is the one mistake on this build that cannot be
> undone by rewiring.

GPIO 34 is input-only and has no internal pull-up, which is exactly what an
analog input wants. It must stay on an ADC1 pin (GPIO 32–39); the ADC2 pins
cannot be read while WiFi is active.

### Centre calibration

Whatever the stick reads at boot is taken as its centre, because real joysticks
do not rest at exactly half scale. The measured value is printed at startup:

```
[joystick] centre measured at 1893 of 4095
```

**Do not hold the stick while the board boots** — that centre would be wrong,
and the motor would creep on its own once released. If that happens, reset the
board without touching the stick.

## Power

The 28BYJ-48 draws roughly **240 mA** while its coils are energised. USB
supplies 500 mA, so driving it from the ESP32's `5V` pin works — but it is a
real fraction of the budget.

If you see the ESP32 resetting when the motor starts, or the serial monitor
reconnecting mid-move, that is a brownout: feed the ULN2003's `+` from a
separate 5 V supply (a phone charger is fine) and **tie its GND to the ESP32's
GND**. The two boards must share a ground or the coil patterns have no
reference.

The firmware drops all four coils the moment a move ends, so the motor draws
essentially nothing while idle. A 64:1 gearbox does not back-drive, so the shaft
stays where it was put without any holding current.

## Pin summary

| Signal | ESP32 GPIO |
|---|---|
| IN1 | 18 |
| IN2 | 19 |
| IN3 | 21 |
| IN4 | 22 |
| Joystick X | 34 (analog, ADC1) |
| status LED | 2 (on-board) |

None are strapping or flash pins on `esp32dev` (WROOM-32).

## Checking it works

The four LEDs on the driver board are the diagnostic, no multimeter needed.

Push the joystick and watch them chase each other in sequence. Return it to
centre and all four go dark: the firmware drops the coils the moment the stick
is centred, so the motor draws nothing while idle.

| What you see | Meaning |
|---|---|
| LEDs chase while the stick is pushed | Working correctly |
| **All four LEDs dark**, always | No 5 V on the board, or `IN1`..`IN4` on the wrong pins |
| **LEDs lit but dim** | Under-powered - check the driver's `+` is on `VIN`, not `3V3` |
| **LEDs chase but the shaft does not turn** | Motor plug not fully seated |
| **Buzzes or twitches without turning** | `IN1`..`IN4` in the wrong order, or the speed is too high |
| Motor creeps with the stick centred | The stick was held during boot - reset without touching it |
| Pushes the wrong way | Set `InvertX = true` in `joystick_task.cpp` |

The serial monitor shows what the joystick task is seeing:

```
[joystick] centre measured at 1893 of 4095
[joystick] jog forward at 2400 us/step
[joystick] centred, stopped
```

No `jog` line when you push the stick means the reading is not moving far
enough from centre - check the X wire on GPIO 34, and that the joystick has
3.3 V.

## If anything gets hot

Disconnect power immediately and let it cool before touching it again.

| Hot part | Cause |
|---|---|
| The 16-pin chip on the ULN2003 board | Power pair reversed, or an `IN` wire shorted to the 5 V rail |
| The small 3-pin regulator next to the ESP32's USB socket | The motor is being fed from **`3V3`** instead of `5V` |

The second is easy to do because those two pins are usually adjacent. The
ESP32's on-board 3.3 V regulator is rated far below the motor's ~240 mA, so it
current-limits, overheats, and browns the board out, which shows up as the USB
port disappearing mid-session. It has thermal shutdown and normally survives.

### Hot chip AND no LEDs at all = reversed power

This exact pair of symptoms cost a long debugging session, so it is worth
recognising on sight:

- **No LED lights, ever**, in any test, so the outputs are not switching
- **The 16-pin chip gets hot within seconds**, so current is definitely flowing

A board with no power would be *cold*. Hot but dead means power is reaching the
chip through the wrong path: the power pair is reversed, and the ULN2003's
internal protection diodes are conducting straight across the supply. The LEDs
never light because they are wired assuming the correct polarity.

The fix is to swap the two power wires, regardless of what the `+`/`-`
silkscreen says. The chip survives a surprising amount of this, but not
indefinitely: power down as soon as it feels hot.
