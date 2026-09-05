#include "core/post.h"

#include <SPI.h>
#include <Wire.h>

#include "core/log.h"
#include "hal/battery.h"
#include "hal/board_pins.h"

// A pack outside this window is either flat or being charged hard; either way
// the node should say so rather than pretend.
static constexpr uint16_t VBAT_PLAUSIBLE_MIN_MV = 3000;
static constexpr uint16_t VBAT_PLAUSIBLE_MAX_MV = 4600;

// Below this there is no battery divider at all - an unpopulated or broken
// sense circuit, not a flat pack. The two cases need different reactions, so
// they are distinguished rather than lumped together as "power bad".
static constexpr uint16_t VBAT_SENSE_FLOOR_MV = 500;

static constexpr uint8_t SX1276_REG_VERSION = 0x42;
static constexpr uint8_t SX1276_VERSION_ID = 0x12;
static constexpr uint8_t SSD1306_I2C_ADDR = 0x3C;

static uint16_t s_mask = 0;
static bool s_nvsOk = true;
static bool s_displayOk = true;
static bool s_radioOk = true;

void postObserveNvs(bool ok) { s_nvsOk = ok; }
void postObserveDisplay(bool ok) { s_displayOk = ok; }
void postObserveRadio(bool ok) { s_radioOk = ok; }

// --- individual checks ----------------------------------------------------
// Each one is side-effect free apart from marking the battery untrusted, so
// they can be run again at any time by `self-test` and, with fake drivers,
// on the host.

static bool chk_power(void) {
  const uint16_t mv = batteryMillivolts();

  if (mv < VBAT_SENSE_FLOOR_MV) {
    // Nothing is on the pin. Reporting this as a flat battery would be a lie,
    // and worse, it would let the low-power gate refuse every flash write for
    // the rest of the node's life over a missing resistor.
    batterySetTrusted(false);
    LOG_E(TAG_BATT, E_BATT_UNTRUSTED, mv);
    return false;
  }

  batterySetTrusted(true);
  if (mv < VBAT_PLAUSIBLE_MIN_MV) LOG_W(TAG_BATT, E_BATT_LOW, mv);
  return mv >= VBAT_PLAUSIBLE_MIN_MV && mv <= VBAT_PLAUSIBLE_MAX_MV;
}

static bool chk_nvs(void) { return s_nvsOk; }

static bool chk_adc(void) {
  // A dead ADC, or an input stuck against a rail, returns the same number
  // every time. Real silicon always jitters by at least one LSB, so a
  // perfectly constant reading is the signature of a fault rather than of a
  // very quiet signal.
  uint16_t first = batteryRawPinMillivolts();
  uint16_t lo = first, hi = first;

  for (uint8_t i = 0; i < 32; i++) {
    const uint16_t v = batteryRawPinMillivolts();
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }

  const bool noisy = (hi != lo);
  if (!noisy) batterySetTrusted(false);
  return noisy;
}

static bool chk_radio(void) {
  // Read the chip's own version register over SPI. It proves the bus, the chip
  // select and the supply all the way to the radio, which "LoRa.begin()
  // returned true" alone does not.
  if (!s_radioOk) return false;

  digitalWrite(LORA_CS, LOW);
  SPI.transfer(SX1276_REG_VERSION & 0x7F); // MSB clear = read
  const uint8_t version = SPI.transfer(0x00);
  digitalWrite(LORA_CS, HIGH);

  return version == SX1276_VERSION_ID;
}

static bool chk_display(void) {
  if (!s_displayOk) return false;

  // An ACK at the panel's address. If the ribbon is loose this is the only
  // thing that notices, and the node is perfectly usable without a screen.
  Wire.beginTransmission(SSD1306_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

static bool chk_button(void) {
  // The key is active-low with a pull-up, so at boot it must read HIGH. A
  // stuck or shorted key would otherwise key the radio continuously, which is
  // both useless and a regulatory problem.
  pinMode(KEY_PIN, INPUT_PULLUP);
  return digitalRead(KEY_PIN) == HIGH;
}

// --- the table ------------------------------------------------------------
// Adding a check has to cost one line, or nobody will ever add one.
//
// `critical` means strictly one thing: without this block the node cannot
// perform its job of moving symbols. It does not mean "stop" - nothing here
// ever stops.

const post_item_t POST_ITEMS[] = {
    {"power", POST_BIT_POWER, chk_power, true,
     "sense broken -> readings untrusted, write gate opens; pack flat -> telemetry only"},
    {"nvs", POST_BIT_NVS, chk_nvs, false,
     "run on firmware defaults and raise the flag; the operator must know settings were lost"},
    {"adc", POST_BIT_ADC, chk_adc, false,
     "battery reading untrusted, low-power write gate disabled"},
    {"radio", POST_BIT_RADIO, chk_radio, true,
     "three init attempts, then degrade: the shell and telemetry stay up"},
    {"display", POST_BIT_DISPLAY, chk_display, false,
     "run headless; UART and radio are unaffected"},
    {"button", POST_BIT_BUTTON, chk_button, false,
     "ignore the key and stay a receive-only node"},
};

const size_t POST_ITEM_COUNT = sizeof(POST_ITEMS) / sizeof(POST_ITEMS[0]);

uint16_t postRun() {
  uint16_t mask = 0;

  for (size_t i = 0; i < POST_ITEM_COUNT; i++) {
    if (POST_ITEMS[i].check()) continue;

    mask |= (uint16_t)(1u << POST_ITEMS[i].bit);
    LOG_E(TAG_POST, E_POST_FAIL, POST_ITEMS[i].bit);
    if (POST_ITEMS[i].critical) LOG_E(TAG_POST, E_POST_CRITICAL, POST_ITEMS[i].bit);
  }

  s_mask = mask;
  LOG_I(TAG_POST, E_POST_MASK, mask);
  if (mask == 0) LOG_I(TAG_POST, E_POST_PASS, 0);

  return mask;
}

uint16_t postMask() { return s_mask; }

uint16_t postCriticalMask() {
  uint16_t critical = 0;
  for (size_t i = 0; i < POST_ITEM_COUNT; i++)
    if (POST_ITEMS[i].critical) critical |= (uint16_t)(1u << POST_ITEMS[i].bit);
  return (uint16_t)(s_mask & critical);
}

const char *postBlockName(uint8_t bit) {
  for (size_t i = 0; i < POST_ITEM_COUNT; i++)
    if (POST_ITEMS[i].bit == bit) return POST_ITEMS[i].name;
  return "?";
}

const char *postBlockReaction(uint8_t bit) {
  for (size_t i = 0; i < POST_ITEM_COUNT; i++)
    if (POST_ITEMS[i].bit == bit) return POST_ITEMS[i].reaction;
  return "";
}

void postPrint(Print &out) {
  out.printf("post    0x%04X %s\n", s_mask, s_mask ? "FAIL" : "OK");
  for (size_t i = 0; i < POST_ITEM_COUNT; i++) {
    const bool failed = (s_mask & (1u << POST_ITEMS[i].bit)) != 0;
    out.printf("  bit %u  %-8s %-4s %s\n", POST_ITEMS[i].bit, POST_ITEMS[i].name,
               failed ? "FAIL" : "ok",
               failed ? POST_ITEMS[i].reaction
                      : (POST_ITEMS[i].critical ? "(critical)" : ""));
  }
}
