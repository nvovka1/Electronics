// Host tests for everything in lib/ that has no registers in it: the frame
// codec, the config schema and its migrations, the A/B record rule and the
// ring log. Two seconds, no board, so nine bugs out of ten are caught here.
//
//   pio test -e native

#include <string.h>
#include <unity.h>

#include "cfg_record.h"
#include "config_schema.h"
#include "crc16.h"
#include "crc32.h"
#include "frame.h"
#include "ring.h"

// The one frame that docs/PROTOCOL.md section 6 walks through byte by byte.
// If this test fails, either the codec changed or the document is now lying;
// both are worth stopping for.
static const uint8_t GOLDEN_SYMBOL_FRAME[] = {
    0xA5, 0x01, 0x01, 0x05, 0x01, 0x00, 0x07, 0x00,
    0x2D, 0x40, 0xE2, 0x01, 0x00, 0x13, 0xA1,
};

static const uint8_t GOLDEN_HEALTH_FRAME[] = {
    0xA5, 0x01, 0x03, 0x0C, 0x01, 0x00, 0x2A, 0x00, 0x1C, 0x9A, 0x40,
    0xE2, 0x01, 0x03, 0x00, 0x27, 0x9F, 0xFF, 0x00, 0x00, 0x62, 0xBA,
};

void setUp(void) {}
void tearDown(void) {}

// --- checksums ------------------------------------------------------------

static void test_crc16_check_value(void) {
  // The published check value for CRC-16/CCITT-FALSE.
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt_false((const uint8_t *)"123456789", 9));
}

static void test_crc16_is_incremental(void) {
  const uint8_t *s = (const uint8_t *)"123456789";
  const uint16_t stepwise = crc16_update(crc16_update(0xFFFF, s, 4), s + 4, 5);
  TEST_ASSERT_EQUAL_HEX16(crc16_ccitt_false(s, 9), stepwise);
}

static void test_crc32_check_value(void) {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_iso((const uint8_t *)"123456789", 9));
}

// --- frame codec ----------------------------------------------------------

static frame_t make_symbol_frame(void) {
  frame_t f;
  memset(&f, 0, sizeof(f));
  f.ver = PROTO_VERSION;
  f.type = MSG_SYMBOL;
  f.src = 1;
  f.seq = 7;
  const payload_symbol_t p = {'-', 123456u};
  f.len = (uint8_t)payload_symbol_put(f.payload, sizeof(f.payload), &p);
  return f;
}

static void test_frame_golden_symbol(void) {
  const frame_t f = make_symbol_frame();
  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&f, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_INT((int)sizeof(GOLDEN_SYMBOL_FRAME), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(GOLDEN_SYMBOL_FRAME, buf, sizeof(GOLDEN_SYMBOL_FRAME));
}

static void test_frame_golden_health(void) {
  frame_t f;
  memset(&f, 0, sizeof(f));
  f.ver = PROTO_VERSION;
  f.type = MSG_HEALTH;
  f.src = 1;
  f.seq = 42;
  const payload_health_t h = {0x9A1C, 123456u, 3, 0, 0x27, -97, 0x0000};
  f.len = (uint8_t)payload_health_put(f.payload, sizeof(f.payload), &h);

  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&f, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_INT(PAYLOAD_HEALTH_SIZE, f.len);
  TEST_ASSERT_EQUAL_INT((int)sizeof(GOLDEN_HEALTH_FRAME), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(GOLDEN_HEALTH_FRAME, buf, sizeof(GOLDEN_HEALTH_FRAME));
}

static void test_frame_roundtrip(void) {
  const frame_t sent = make_symbol_frame();
  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&sent, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN_INT(0, n);

  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(buf, (size_t)n, &got));
  TEST_ASSERT_EQUAL_UINT8(sent.ver, got.ver);
  TEST_ASSERT_EQUAL_UINT8(sent.type, got.type);
  TEST_ASSERT_EQUAL_UINT16(sent.src, got.src);
  TEST_ASSERT_EQUAL_UINT16(sent.seq, got.seq);
  TEST_ASSERT_EQUAL_UINT8(sent.len, got.len);

  payload_symbol_t p;
  TEST_ASSERT_EQUAL_size_t(PAYLOAD_SYMBOL_SIZE, payload_symbol_get(got.payload, got.len, &p));
  TEST_ASSERT_EQUAL_UINT8('-', p.symbol);
  TEST_ASSERT_EQUAL_UINT32(123456u, p.key_ms);
}

static void test_frame_ack_roundtrips_with_wrapped_seq(void) {
  frame_t f;
  memset(&f, 0, sizeof(f));
  f.ver = PROTO_VERSION;
  f.type = MSG_ACK;
  f.src = 900;
  f.seq = 0xFFFF; // the wrap value must survive the codec
  const payload_ack_t a = {0x1234, -97, -14};
  f.len = (uint8_t)payload_ack_put(f.payload, sizeof(f.payload), &a);

  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&f, buf, sizeof(buf));
  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(buf, (size_t)n, &got));

  payload_ack_t back;
  TEST_ASSERT_EQUAL_size_t(PAYLOAD_ACK_SIZE, payload_ack_get(got.payload, got.len, &back));
  TEST_ASSERT_EQUAL_UINT16(0x1234, back.ack_seq);
  TEST_ASSERT_EQUAL_INT8(-97, back.rssi);
  TEST_ASSERT_EQUAL_INT8(-14, back.snr_q2);
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, got.seq);
}

static void test_frame_rejects_bad_sync(void) {
  uint8_t buf[sizeof(GOLDEN_SYMBOL_FRAME)];
  memcpy(buf, GOLDEN_SYMBOL_FRAME, sizeof(buf));
  buf[0] = 0x5A;

  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_SYNC, frame_decode(buf, sizeof(buf), &got));
}

static void test_frame_rejects_bad_version(void) {
  uint8_t buf[sizeof(GOLDEN_SYMBOL_FRAME)];
  memcpy(buf, GOLDEN_SYMBOL_FRAME, sizeof(buf));
  buf[1] = 99;

  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_VERSION, frame_decode(buf, sizeof(buf), &got));
}

static void test_frame_rejects_single_bit_flip(void) {
  // A flipped payload bit is exactly what the CRC exists for.
  uint8_t buf[sizeof(GOLDEN_SYMBOL_FRAME)];
  memcpy(buf, GOLDEN_SYMBOL_FRAME, sizeof(buf));
  buf[8] ^= 0x01;

  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, frame_decode(buf, sizeof(buf), &got));
}

static void test_frame_rejects_length_disagreement(void) {
  uint8_t buf[sizeof(GOLDEN_SYMBOL_FRAME)];
  memcpy(buf, GOLDEN_SYMBOL_FRAME, sizeof(buf));
  buf[3] = 40; // LEN claims far more than arrived

  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_LENGTH, frame_decode(buf, sizeof(buf), &got));
}

static void test_frame_rejects_runt(void) {
  frame_t got;
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_SHORT, frame_decode(GOLDEN_SYMBOL_FRAME, 9, &got));
}

static void test_frame_unknown_type_still_parses_header(void) {
  // An unknown type is reported, but SRC and SEQ are filled in first: they are
  // what makes the event worth logging at all.
  frame_t f = make_symbol_frame();
  f.type = 0x41; // vendor range
  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&f, buf, sizeof(buf));

  frame_t got;
  memset(&got, 0, sizeof(got));
  TEST_ASSERT_EQUAL_INT(FRAME_ERR_TYPE, frame_decode(buf, (size_t)n, &got));
  TEST_ASSERT_EQUAL_UINT16(1, got.src);
  TEST_ASSERT_EQUAL_UINT16(7, got.seq);
  TEST_ASSERT_EQUAL_STRING("VENDOR", frame_type_name(0x41));
}

static void test_frame_refuses_oversize_payload(void) {
  frame_t f = make_symbol_frame();
  f.len = FRAME_MAX_PAYLOAD + 1;
  uint8_t buf[FRAME_MAX_SIZE];
  TEST_ASSERT_EQUAL_INT(-FRAME_ERR_LENGTH, frame_encode(&f, buf, sizeof(buf)));
}

static void test_frame_refuses_small_output_buffer(void) {
  const frame_t f = make_symbol_frame();
  uint8_t buf[4];
  TEST_ASSERT_EQUAL_INT(-FRAME_ERR_ARG, frame_encode(&f, buf, sizeof(buf)));
}

// --- config schema --------------------------------------------------------

static void test_config_struct_layout_is_stable(void) {
  // The CRC is taken over the raw bytes, so a hole in the struct would make a
  // byte-identical config look corrupt. Both versions must stay packed, and
  // cfg_version must stay at offset 4 in every one of them.
  TEST_ASSERT_EQUAL_size_t(16, sizeof(config_v1_t));
  TEST_ASSERT_EQUAL_size_t(20, sizeof(config_v2_t));
  TEST_ASSERT_EQUAL_size_t(28, sizeof(config_v3_t));

  // v4 spends the padding byte v3 was already carrying, so the record does not
  // change size. Two versions of the same size is fine - cfg_version is what
  // identifies a blob - but it means the size check in config_migrate() cannot
  // catch a v3 record mislabelled as v4, so this assertion is the thing keeping
  // that from happening by accident.
  TEST_ASSERT_EQUAL_size_t(28, sizeof(config_v4_t));
  TEST_ASSERT_EQUAL_size_t(32, sizeof(config_v5_t));
  TEST_ASSERT_EQUAL_size_t(4, offsetof(config_v5_t, cfg_version));
  TEST_ASSERT_EQUAL_size_t(4, offsetof(config_v1_t, cfg_version));
  TEST_ASSERT_EQUAL_size_t(4, offsetof(config_v2_t, cfg_version));
  TEST_ASSERT_EQUAL_size_t(4, offsetof(config_v3_t, cfg_version));
  TEST_ASSERT_EQUAL_size_t(4, offsetof(config_v4_t, cfg_version));

  // Every version has to fit the container it is stored in, and the check
  // belongs here rather than in a comment: the day a v4 outgrows the blob is
  // the day every node in the field silently falls back to defaults.
  TEST_ASSERT_TRUE(sizeof(config_t) <= CFG_BLOB_MAX);
}

static void test_config_defaults_validate(void) {
  config_t c;
  config_defaults(&c, 1);
  const char *bad = nullptr;
  TEST_ASSERT_EQUAL_INT(0, config_validate(&c, &bad));
  TEST_ASSERT_NULL(bad);
  TEST_ASSERT_EQUAL_UINT16(CFG_VERSION_CURRENT, c.cfg_version);
  TEST_ASSERT_EQUAL_UINT8(0x2B, c.sync_word); // not the library's stock 0x12
}

static void test_config_every_field_accepts_min_and_max(void) {
  for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
    config_t c;
    config_defaults(&c, 1);
    const cfg_field_t *f = &CFG_FIELDS[i];

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, config_field_set(&c, f, f->min), f->name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(f->min, config_field_get(&c, f), f->name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, config_field_set(&c, f, f->max), f->name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(f->max, config_field_get(&c, f), f->name);
  }
}

static void test_config_every_field_refuses_out_of_range(void) {
  for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
    config_t c;
    config_defaults(&c, 1);
    const cfg_field_t *f = &CFG_FIELDS[i];
    const uint32_t before = config_field_get(&c, f);

    if (f->min > 0)
      TEST_ASSERT_EQUAL_INT_MESSAGE(-1, config_field_set(&c, f, f->min - 1), f->name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, config_field_set(&c, f, f->max + 1), f->name);

    // A refused value must leave the config untouched, not half-applied.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, config_field_get(&c, f), f->name);
  }
}

static void test_config_unknown_field_is_not_found(void) {
  TEST_ASSERT_NULL(config_field_by_name("thr_hi"));
  TEST_ASSERT_NULL(config_field_by_name("cfg_version")); // shape, not a setting
  TEST_ASSERT_NOT_NULL(config_field_by_name("tx_power"));
}

static void test_config_validate_names_the_offender(void) {
  config_t c;
  config_defaults(&c, 1);
  c.tx_power = 99; // written past the setter, as a corrupt blob would be

  const char *bad = nullptr;
  TEST_ASSERT_EQUAL_INT(-1, config_validate(&c, &bad));
  TEST_ASSERT_EQUAL_STRING("tx_power", bad);
}

// --- migration ------------------------------------------------------------

static config_v1_t make_v1(void) {
  config_v1_t v1;
  memset(&v1, 0, sizeof(v1));
  v1.cfg_version = 1;
  v1.freq_hz = 868100000u;
  v1.node_id = 7;
  v1.period_s = 120;
  v1.log_level = 4;
  v1.tx_power = 11;
  v1.spreading = 9;
  return v1;
}

static void test_migrate_1_2_carries_old_and_defaults_new(void) {
  const config_v1_t v1 = make_v1();
  config_v2_t v2;
  config_migrate_1_2(&v1, &v2);

  // Carried across by name, never by memcpy.
  TEST_ASSERT_EQUAL_UINT16(7, v2.node_id);
  TEST_ASSERT_EQUAL_UINT32(868100000u, v2.freq_hz);
  TEST_ASSERT_EQUAL_UINT8(4, v2.log_level);
  TEST_ASSERT_EQUAL_UINT8(11, v2.tx_power);
  TEST_ASSERT_EQUAL_UINT8(9, v2.spreading);
  TEST_ASSERT_EQUAL_UINT16(120, v2.health_period_s); // the v1 period keeps its value

  // New fields take the firmware's defaults, never a zero out of flash.
  TEST_ASSERT_EQUAL_UINT8(5, v2.coding_rate);
  TEST_ASSERT_EQUAL_UINT8(0x2B, v2.sync_word);
  TEST_ASSERT_EQUAL_UINT16(600, v2.ack_timeout_ms);
  TEST_ASSERT_EQUAL_UINT8(3, v2.ack_retries);
  TEST_ASSERT_EQUAL_UINT16(3300, v2.vbat_min_mv);

  TEST_ASSERT_EQUAL_UINT16(2, v2.cfg_version);
}

static void test_migrate_2_3_carries_old_and_defaults_new(void) {
  config_v2_t v2;
  memset(&v2, 0, sizeof(v2));
  v2.cfg_version = 2;
  v2.freq_hz = 869500000u;
  v2.node_id = 42;
  v2.health_period_s = 900;
  v2.ack_timeout_ms = 1200;
  v2.vbat_min_mv = 3400;
  v2.log_level = 2;
  v2.tx_power = 17;
  v2.spreading = 10;
  v2.coding_rate = 8;
  v2.sync_word = 0x77;
  v2.ack_retries = 5;

  config_v3_t v3;
  config_migrate_2_3(&v2, &v3);

  // Every field v2 knew arrives with the operator's value, not a default.
  // This is the whole point of the hop: a node that was tuned for a difficult
  // site must not come back from an update on factory settings.
  TEST_ASSERT_EQUAL_UINT32(869500000u, v3.freq_hz);
  TEST_ASSERT_EQUAL_UINT16(42, v3.node_id);
  TEST_ASSERT_EQUAL_UINT16(900, v3.health_period_s);
  TEST_ASSERT_EQUAL_UINT16(1200, v3.ack_timeout_ms);
  TEST_ASSERT_EQUAL_UINT16(3400, v3.vbat_min_mv);
  TEST_ASSERT_EQUAL_UINT8(2, v3.log_level);
  TEST_ASSERT_EQUAL_UINT8(17, v3.tx_power);
  TEST_ASSERT_EQUAL_UINT8(10, v3.spreading);
  TEST_ASSERT_EQUAL_UINT8(8, v3.coding_rate);
  TEST_ASSERT_EQUAL_UINT8(0x77, v3.sync_word);
  TEST_ASSERT_EQUAL_UINT8(5, v3.ack_retries);

  // The five fields v2 had never heard of come from the firmware's defaults.
  TEST_ASSERT_EQUAL_UINT8(1, v3.wifi_enabled);
  TEST_ASSERT_EQUAL_UINT16(300, v3.report_period_s);
  TEST_ASSERT_EQUAL_UINT8(1, v3.tls_verify);
  TEST_ASSERT_EQUAL_UINT8(1, v3.ota_enabled);
  TEST_ASSERT_EQUAL_UINT16(3600, v3.ota_vbat_min_mv);

  // The OTA gate must sit above the config-write gate. An update that is
  // allowed on a cell too flat to finish it is the brick this whole scheme
  // exists to prevent.
  TEST_ASSERT_TRUE(v3.ota_vbat_min_mv > v3.vbat_min_mv);

  TEST_ASSERT_EQUAL_UINT16(3, v3.cfg_version);

  // config_validate() is deliberately not called here. It only ever judges the
  // CURRENT version - that is what makes it the right check for a config about
  // to be used - so an intermediate hop is validated at the end of the chain,
  // by test_migrate_chain_from_blob, and not halfway along it.
}

static void test_migrate_3_4_lowers_the_wifi_transmit_power(void) {
  config_v3_t v3;
  memset(&v3, 0, sizeof(v3));
  v3.cfg_version = 3;
  v3.freq_hz = 868300000u;
  v3.node_id = 12;
  v3.health_period_s = 120;
  v3.ack_timeout_ms = 900;
  v3.vbat_min_mv = 3350;
  v3.report_period_s = 600;
  v3.ota_vbat_min_mv = 3700;
  v3.log_level = 4;
  v3.tx_power = 18;
  v3.spreading = 11;
  v3.coding_rate = 6;
  v3.sync_word = 0x2B;
  v3.ack_retries = 2;
  v3.wifi_enabled = 1;
  v3.ota_enabled = 0; // an operator turned updates off on this node
  v3.tls_verify = 1;

  config_v4_t v4;
  config_migrate_3_4(&v3, &v4);

  // Every v3 field arrives untouched. ota_enabled in particular: a node
  // somebody deliberately took out of the update rota must not quietly rejoin
  // it because of a migration.
  TEST_ASSERT_EQUAL_UINT32(868300000u, v4.freq_hz);
  TEST_ASSERT_EQUAL_UINT16(12, v4.node_id);
  TEST_ASSERT_EQUAL_UINT16(120, v4.health_period_s);
  TEST_ASSERT_EQUAL_UINT16(900, v4.ack_timeout_ms);
  TEST_ASSERT_EQUAL_UINT16(3350, v4.vbat_min_mv);
  TEST_ASSERT_EQUAL_UINT16(600, v4.report_period_s);
  TEST_ASSERT_EQUAL_UINT16(3700, v4.ota_vbat_min_mv);
  TEST_ASSERT_EQUAL_UINT8(4, v4.log_level);
  TEST_ASSERT_EQUAL_UINT8(18, v4.tx_power); // the LoRa transmitter, unchanged
  TEST_ASSERT_EQUAL_UINT8(11, v4.spreading);
  TEST_ASSERT_EQUAL_UINT8(6, v4.coding_rate);
  TEST_ASSERT_EQUAL_UINT8(2, v4.ack_retries);
  TEST_ASSERT_EQUAL_UINT8(1, v4.wifi_enabled);
  TEST_ASSERT_EQUAL_UINT8(0, v4.ota_enabled);
  TEST_ASSERT_EQUAL_UINT8(1, v4.tls_verify);

  // The new field, and the reason for the release: a v3 node transmitted at
  // whatever the radio defaulted to, which is 19.5 dBm, and on a board with no
  // battery that burst is what causes the brownout.
  TEST_ASSERT_EQUAL_UINT8(13, v4.wifi_tx_dbm);
  TEST_ASSERT_TRUE(v4.wifi_tx_dbm < 19);

  TEST_ASSERT_EQUAL_UINT16(4, v4.cfg_version);

  // Not validated here: config_validate() only ever judges the CURRENT
  // version, which is what makes it the right check for a config about to be
  // used. Intermediate hops are validated at the end of the chain instead.
}

static void test_migrate_3_4_from_blob(void) {
  // The hop the one board already running fw 1.1.0 takes on this release.
  config_v3_t v3;
  memset(&v3, 0, sizeof(v3));
  v3.cfg_version = 3;
  v3.freq_hz = 868000000u;
  v3.node_id = 191;
  v3.health_period_s = 60;
  v3.ack_timeout_ms = 600;
  v3.vbat_min_mv = 3300;
  v3.report_period_s = 300;
  v3.ota_vbat_min_mv = 3600;
  v3.log_level = 3;
  v3.tx_power = 14;
  v3.spreading = 7;
  v3.coding_rate = 5;
  v3.sync_word = 0x2B;
  v3.ack_retries = 3;
  v3.wifi_enabled = 1;
  v3.ota_enabled = 1;
  v3.tls_verify = 1;

  config_t out;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(&v3, sizeof(v3), 3, &out));
  TEST_ASSERT_EQUAL_UINT16(CFG_VERSION_CURRENT, out.cfg_version);
  TEST_ASSERT_EQUAL_UINT16(191, out.node_id);
  TEST_ASSERT_EQUAL_UINT8(13, out.wifi_tx_dbm);

  // v3 and v4 are the same size, so only the claimed version separates them.
  // A v3 blob labelled v4 is copied through verbatim and its padding byte
  // becomes wifi_tx_dbm, which would be 0 - outside the field's 2..20 range.
  // config_validate() is what catches that, and configBegin() falls back to
  // defaults rather than transmitting at a level the radio cannot produce.
  config_v3_t mislabelled = v3;
  mislabelled.cfg_version = 4;
  config_t wrong;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(&mislabelled, sizeof(mislabelled), 4, &wrong));
  const char *bad = nullptr;
  TEST_ASSERT_EQUAL_INT(-1, config_validate(&wrong, &bad));
  TEST_ASSERT_EQUAL_STRING("wifi_tx_dbm", bad);
}

static void test_migrate_4_5_keeps_the_battery_gates_on(void) {
  config_v4_t v4;
  memset(&v4, 0, sizeof(v4));
  v4.cfg_version = 4;
  v4.freq_hz = 868000000u;
  v4.node_id = 5;
  v4.health_period_s = 60;
  v4.ack_timeout_ms = 600;
  v4.vbat_min_mv = 3300;
  v4.report_period_s = 300;
  v4.ota_vbat_min_mv = 3600;
  v4.log_level = 3;
  v4.tx_power = 14;
  v4.spreading = 7;
  v4.coding_rate = 5;
  v4.sync_word = 0x2B;
  v4.ack_retries = 3;
  v4.wifi_enabled = 1;
  v4.ota_enabled = 1;
  v4.tls_verify = 1;
  v4.wifi_tx_dbm = 13;

  config_v5_t v5;
  config_migrate_4_5(&v4, &v5);

  TEST_ASSERT_EQUAL_UINT16(5, v5.node_id);
  TEST_ASSERT_EQUAL_UINT8(13, v5.wifi_tx_dbm);
  TEST_ASSERT_EQUAL_UINT16(3600, v5.ota_vbat_min_mv);

  // The whole point of this hop: has_battery arrives as 1, so a fleet updating
  // to v5 keeps every charge gate it had. A migration must never switch a
  // safety gate OFF across a whole fleet - a node that really is on USB is told
  // so by an operator, once, deliberately.
  TEST_ASSERT_EQUAL_UINT8(1, v5.has_battery);

  TEST_ASSERT_EQUAL_UINT16(5, v5.cfg_version);

  const char *bad = nullptr;
  TEST_ASSERT_EQUAL_INT(0, config_validate(&v5, &bad));
}

static void test_migrate_chain_from_blob(void) {
  // A node that has been in a drawer since fw 1.0.0: its record is v1 and it
  // has to walk 1 -> 2 -> 3 in one boot, carrying its settings the whole way.
  const config_v1_t v1 = make_v1();
  config_t out;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(&v1, sizeof(v1), 1, &out));
  TEST_ASSERT_EQUAL_UINT16(CFG_VERSION_CURRENT, out.cfg_version);
  TEST_ASSERT_EQUAL_UINT8(11, out.tx_power);
  TEST_ASSERT_EQUAL_UINT16(120, out.health_period_s); // survived two hops
  TEST_ASSERT_EQUAL_UINT16(7, out.node_id);

  // And it arrives with the uplink configured, which is what makes the
  // update reach it next time without anyone driving out.
  TEST_ASSERT_EQUAL_UINT8(1, out.wifi_enabled);
  TEST_ASSERT_EQUAL_UINT16(300, out.report_period_s);
  TEST_ASSERT_EQUAL_UINT8(13, out.wifi_tx_dbm); // survived the whole chain
  TEST_ASSERT_EQUAL_UINT8(1, out.has_battery);

  const char *bad = nullptr;
  TEST_ASSERT_EQUAL_INT(0, config_validate(&out, &bad));
}

static void test_migrate_2_3_from_blob(void) {
  // The hop an existing fleet actually takes on this release.
  config_v2_t v2;
  memset(&v2, 0, sizeof(v2));
  v2.cfg_version = 2;
  v2.freq_hz = 868000000u;
  v2.node_id = 3;
  v2.health_period_s = 60;
  v2.ack_timeout_ms = 600;
  v2.vbat_min_mv = 3300;
  v2.log_level = 3;
  v2.tx_power = 20;
  v2.spreading = 7;
  v2.coding_rate = 5;
  v2.sync_word = 0x2B;
  v2.ack_retries = 3;

  config_t out;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(&v2, sizeof(v2), 2, &out));
  TEST_ASSERT_EQUAL_UINT16(CFG_VERSION_CURRENT, out.cfg_version);
  TEST_ASSERT_EQUAL_UINT8(20, out.tx_power);

  // A v2-sized blob that claims to be v1 is refused rather than read as one.
  config_t ignored;
  TEST_ASSERT_EQUAL_INT(-1, config_migrate(&v2, sizeof(v2), 1, &ignored));
}

static void test_migrate_passes_current_version_through(void) {
  config_t c;
  config_defaults(&c, 5);
  c.tx_power = 17;

  config_t out;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(&c, sizeof(c), CFG_VERSION_CURRENT, &out));
  TEST_ASSERT_EQUAL_UINT8(17, out.tx_power);
  TEST_ASSERT_EQUAL_UINT16(5, out.node_id);
}

static void test_migrate_refuses_wrong_size_and_future_version(void) {
  const config_v1_t v1 = make_v1();
  config_t out;

  // The blob claims v1 but is not v1-sized: refuse rather than read past it.
  TEST_ASSERT_EQUAL_INT(-1, config_migrate(&v1, sizeof(v1) - 2, 1, &out));

  // A config written by a NEWER firmware. Guessing at unknown fields would
  // corrupt them, so this is an honest refusal, not a silent reinterpretation.
  TEST_ASSERT_EQUAL_INT(-1, config_migrate(&v1, sizeof(v1), 99, &out));
}

// --- A/B records ----------------------------------------------------------

static cfg_record_t make_record(uint32_t seq, uint8_t tx_power) {
  config_t c;
  config_defaults(&c, 1);
  c.tx_power = tx_power;

  cfg_record_t r;
  cfg_record_build(&r, seq, CFG_VERSION_CURRENT, &c, sizeof(c));
  return r;
}

static void test_record_roundtrips(void) {
  const cfg_record_t r = make_record(1, 17);
  TEST_ASSERT_TRUE(cfg_record_valid(&r));

  config_t c;
  TEST_ASSERT_EQUAL_INT(0, config_migrate(r.blob, r.size, r.cfg_version, &c));
  TEST_ASSERT_EQUAL_UINT8(17, c.tx_power);
}

static void test_record_detects_a_torn_write(void) {
  cfg_record_t r = make_record(1, 17);
  r.blob[3] ^= 0x20; // a byte that never made it to flash intact
  TEST_ASSERT_FALSE(cfg_record_valid(&r));
}

static void test_record_rejects_erased_flash(void) {
  cfg_record_t r;
  memset(&r, 0xFF, sizeof(r)); // what an erased sector reads back as
  TEST_ASSERT_FALSE(cfg_record_valid(&r));

  memset(&r, 0x00, sizeof(r));
  TEST_ASSERT_FALSE(cfg_record_valid(&r));
}

static void test_slot_pick_takes_the_newest_valid(void) {
  const cfg_record_t a = make_record(16, 14);
  const cfg_record_t b = make_record(17, 17);
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_B, cfg_record_pick(&a, true, &b, true));
}

static void test_slot_pick_ignores_the_torn_one(void) {
  // This is the whole point: the power went out while B was being written,
  // so the device must come up on A's older but complete value.
  const cfg_record_t a = make_record(16, 14);
  cfg_record_t b = make_record(17, 17);
  b.blob[1] ^= 0xFF;

  TEST_ASSERT_EQUAL_INT(CFG_SLOT_A, cfg_record_pick(&a, true, &b, true));
}

static void test_slot_pick_handles_first_boot_and_total_loss(void) {
  const cfg_record_t a = make_record(1, 14);

  TEST_ASSERT_EQUAL_INT(CFG_SLOT_A, cfg_record_pick(&a, true, nullptr, false));
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_B, cfg_record_pick(nullptr, false, &a, true));
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_NONE, cfg_record_pick(nullptr, false, nullptr, false));
}

static void test_slot_pick_is_deterministic_on_a_tie(void) {
  const cfg_record_t a = make_record(9, 14);
  const cfg_record_t b = make_record(9, 17);
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_A, cfg_record_pick(&a, true, &b, true));
}

static void test_next_slot_never_targets_the_active_one(void) {
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_B, cfg_record_next_slot(CFG_SLOT_A));
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_A, cfg_record_next_slot(CFG_SLOT_B));
  TEST_ASSERT_EQUAL_INT(CFG_SLOT_A, cfg_record_next_slot(CFG_SLOT_NONE));
}

// --- ring log -------------------------------------------------------------

static log_rec_t storage[8];
static log_ring_t ring;

static void push_n(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    const log_rec_t r = {i, 2, 1, 0, 0, i};
    ring_push(&ring, &r);
  }
}

static void test_ring_holds_what_fits(void) {
  ring_init(&ring, storage, 8);
  push_n(5);

  TEST_ASSERT_EQUAL_UINT16(5, ring_count(&ring));
  TEST_ASSERT_EQUAL_UINT32(0, ring_dropped(&ring));

  log_rec_t out;
  TEST_ASSERT_TRUE(ring_peek_newest(&ring, 0, &out));
  TEST_ASSERT_EQUAL_UINT32(4, out.arg);
  TEST_ASSERT_TRUE(ring_peek_newest(&ring, 4, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.arg);
  TEST_ASSERT_FALSE(ring_peek_newest(&ring, 5, &out));
}

static void test_ring_overwrites_the_oldest(void) {
  ring_init(&ring, storage, 8);
  push_n(300); // far past the wrap, and past a byte counter

  TEST_ASSERT_EQUAL_UINT16(8, ring_count(&ring));
  TEST_ASSERT_EQUAL_UINT32(292, ring_dropped(&ring));

  // Newest first, in order, with nothing shuffled by the wrap.
  for (uint16_t i = 0; i < 8; i++) {
    log_rec_t out;
    TEST_ASSERT_TRUE(ring_peek_newest(&ring, i, &out));
    TEST_ASSERT_EQUAL_UINT32(299u - i, out.arg);
  }
}

static void test_ring_survives_no_storage(void) {
  log_ring_t empty;
  ring_init(&empty, nullptr, 8);
  const log_rec_t r = {1, 2, 3, 4, 0, 5};
  ring_push(&empty, &r); // must not crash: logging is never allowed to fault
  TEST_ASSERT_EQUAL_UINT16(0, ring_count(&empty));
}

static void test_record_size_is_twelve_bytes(void) {
  // Twelve bytes is the whole argument for codes over text in the field.
  TEST_ASSERT_EQUAL_size_t(12, sizeof(log_rec_t));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_crc16_is_incremental);
  RUN_TEST(test_crc32_check_value);

  RUN_TEST(test_frame_golden_symbol);
  RUN_TEST(test_frame_golden_health);
  RUN_TEST(test_frame_roundtrip);
  RUN_TEST(test_frame_ack_roundtrips_with_wrapped_seq);
  RUN_TEST(test_frame_rejects_bad_sync);
  RUN_TEST(test_frame_rejects_bad_version);
  RUN_TEST(test_frame_rejects_single_bit_flip);
  RUN_TEST(test_frame_rejects_length_disagreement);
  RUN_TEST(test_frame_rejects_runt);
  RUN_TEST(test_frame_unknown_type_still_parses_header);
  RUN_TEST(test_frame_refuses_oversize_payload);
  RUN_TEST(test_frame_refuses_small_output_buffer);

  RUN_TEST(test_config_struct_layout_is_stable);
  RUN_TEST(test_config_defaults_validate);
  RUN_TEST(test_config_every_field_accepts_min_and_max);
  RUN_TEST(test_config_every_field_refuses_out_of_range);
  RUN_TEST(test_config_unknown_field_is_not_found);
  RUN_TEST(test_config_validate_names_the_offender);

  RUN_TEST(test_migrate_1_2_carries_old_and_defaults_new);
  RUN_TEST(test_migrate_2_3_carries_old_and_defaults_new);
  RUN_TEST(test_migrate_3_4_lowers_the_wifi_transmit_power);
  RUN_TEST(test_migrate_4_5_keeps_the_battery_gates_on);
  RUN_TEST(test_migrate_chain_from_blob);
  RUN_TEST(test_migrate_2_3_from_blob);
  RUN_TEST(test_migrate_3_4_from_blob);
  RUN_TEST(test_migrate_passes_current_version_through);
  RUN_TEST(test_migrate_refuses_wrong_size_and_future_version);

  RUN_TEST(test_record_roundtrips);
  RUN_TEST(test_record_detects_a_torn_write);
  RUN_TEST(test_record_rejects_erased_flash);
  RUN_TEST(test_slot_pick_takes_the_newest_valid);
  RUN_TEST(test_slot_pick_ignores_the_torn_one);
  RUN_TEST(test_slot_pick_handles_first_boot_and_total_loss);
  RUN_TEST(test_slot_pick_is_deterministic_on_a_tie);
  RUN_TEST(test_next_slot_never_targets_the_active_one);

  RUN_TEST(test_ring_holds_what_fits);
  RUN_TEST(test_ring_overwrites_the_oldest);
  RUN_TEST(test_ring_survives_no_storage);
  RUN_TEST(test_record_size_is_twelve_bytes);

  return UNITY_END();
}
