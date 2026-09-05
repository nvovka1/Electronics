"""The known-face LED.

Wiring: LED anode -> ~330 ohm resistor -> physical pin 11 (BCM GPIO 17);
cathode -> any ground pin, e.g. physical pin 9.

A missing wire, a missing library or a busy pin must never stop the app: in any
of those cases this falls back to a light that does nothing.
"""
from __future__ import annotations

import sys
import time


class NullLed:
    """Used when the LED is disabled or the GPIO is unavailable."""

    def set(self, on: bool) -> None:
        pass

    def close(self) -> None:
        pass


class HoldingLed:
    """On immediately; off only after `hold_ms` of continuous off requests.

    The hold exists because recognition drops out for a frame or two quite
    normally - without it the LED strobes while somebody sits still.

    `set` is expected to be called every frame; the turn-off happens on a later
    call, not on a timer of its own.
    """

    def __init__(self, backend, hold_ms: int, now=time.monotonic) -> None:
        self._backend = backend
        self._hold_seconds = hold_ms / 1000.0
        self._now = now
        self._on = False
        self._off_requested_at = None

    def set(self, on: bool) -> None:
        if on:
            self._off_requested_at = None
            if not self._on:
                self._backend.on()
                self._on = True
            return

        if not self._on:
            return

        if self._off_requested_at is None:
            self._off_requested_at = self._now()
        elif self._now() - self._off_requested_at >= self._hold_seconds:
            self._backend.off()
            self._on = False
            self._off_requested_at = None

    def close(self) -> None:
        if self._on:
            self._backend.off()
            self._on = False
        self._backend.close()


def create_led(enabled: bool, pin: int, hold_ms: int, active_high: bool = True):
    """Build the LED. `active_high` must match the wiring - see config.py.

    gpiozero handles the inversion itself, so the rest of the application only
    ever says "on" or "off" and never has to know which way round it is."""
    if not enabled:
        return NullLed()

    try:
        from gpiozero import LED
    except ImportError:
        print("gpiozero is not installed; running without the LED.", file=sys.stderr)
        return NullLed()

    try:
        return HoldingLed(LED(pin, active_high=active_high), hold_ms)
    except Exception as error:
        print(f"Could not use GPIO {pin} ({error}); running without the LED.",
              file=sys.stderr)
        return NullLed()
