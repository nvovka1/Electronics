"""Check that the LED wiring and the led_active_high setting agree.

    python3 tools/check_led.py

It announces each state before entering it and holds for three seconds, so you
can watch the LED and confirm it matches. If the LED does the opposite of what
is announced, flip led_active_high in src/config.py.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.config import CONFIG  # noqa: E402
from src.led import create_led  # noqa: E402

HOLD_SECONDS = 3


def main() -> int:
    wiring = "active high" if CONFIG.led_active_high else "active low"
    print(f"GPIO {CONFIG.led_pin}, configured as {wiring}.")
    print("Watch the LED and check it does what each line says.\n")

    # Hold of 0 ms, so "off" takes effect at once instead of waiting out the
    # anti-flicker delay the application relies on.
    led = create_led(CONFIG.led_enabled, CONFIG.led_pin, 0, CONFIG.led_active_high)

    try:
        for round_number in (1, 2):
            print(f"round {round_number}: LED should be ON  (known face)")
            led.set(True)
            time.sleep(HOLD_SECONDS)

            print(f"round {round_number}: LED should be OFF (no known face)")
            led.set(False)
            led.set(False)  # the second call is what completes the turn-off
            time.sleep(HOLD_SECONDS)
    finally:
        led.close()

    print(
        "\nIf the LED was on when the line said OFF, set "
        f"led_active_high = {not CONFIG.led_active_high} in src/config.py."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
