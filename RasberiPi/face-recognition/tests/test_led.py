from src.led import HoldingLed, NullLed, create_led

HOLD_MS = 500


class FakeBackend:
    def __init__(self):
        self.events = []

    def on(self):
        self.events.append("on")

    def off(self):
        self.events.append("off")

    def close(self):
        self.events.append("close")


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


def test_turning_on_reaches_the_hardware_immediately():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(True)

    assert backend.events == ["on"]


def test_staying_on_does_not_repeat_the_command():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(True)
    led.set(True)
    led.set(True)

    assert backend.events == ["on"]


def test_a_brief_dropout_does_not_turn_the_led_off():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.set(False)          # starts the hold
    clock.advance(0.2)
    led.set(False)
    led.set(True)           # back before the hold expired

    assert backend.events == ["on"]


def test_the_led_turns_off_once_the_hold_expires():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.set(False)          # starts the hold
    clock.advance(0.6)
    led.set(False)          # hold expired

    assert backend.events == ["on", "off"]


def test_setting_off_on_an_already_off_led_does_nothing():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(False)
    clock.advance(10.0)
    led.set(False)

    assert backend.events == []


def test_closing_turns_a_lit_led_off():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.close()

    assert backend.events == ["on", "off", "close"]


def test_a_disabled_led_is_a_null_led():
    led = create_led(enabled=False, pin=17, hold_ms=HOLD_MS)

    assert isinstance(led, NullLed)
    led.set(True)
    led.close()
