#include "tasks/radio_task.h"

#include <LoRa.h>
#include <SPI.h>

#include "core/config.h"
#include "core/health.h"
#include "core/log.h"
#include "frame.h"
#include "hal/board_pins.h"
#include "tasks/tone.h"
#include "tasks/ui_task.h"

// How long the task blocks on its queue before polling the receiver again.
constexpr uint32_t RADIO_POLL_MS = 20;

constexpr UBaseType_t RADIO_QUEUE_LENGTH = 8;

// Enough to catch a retransmission of something already delivered. A longer
// history costs RAM and buys nothing: a duplicate that arrives after eight
// other frames is not a retry, it is a different problem.
constexpr uint8_t DEDUP_HISTORY = 8;

static QueueHandle_t radioQueue = nullptr;
static TaskHandle_t radioTaskHandle = nullptr;

// The radio task owns the SPI bus, but `self-test` re-runs the POST from the
// shell task, which needs to probe the chip. One mutex, held only for the
// duration of a bus operation, keeps that from landing in the middle of a
// transaction.
static SemaphoreHandle_t s_busMutex = nullptr;

static constexpr uint8_t SX1276_REG_VERSION = 0x42;
static constexpr uint8_t SX1276_VERSION_ID = 0x12;

static bool busTake(TickType_t timeout) {
  if (!s_busMutex) return true;  // before the mutex exists, setup() is single-threaded
  return xSemaphoreTake(s_busMutex, timeout) == pdTRUE;
}

static void busGive() {
  if (s_busMutex) xSemaphoreGive(s_busMutex);
}

static uint16_t s_txSeq = 0;
static int s_lastRssi = 0;
static float s_lastSnr = 0.0f;

static uint32_t s_txCount = 0;
static uint32_t s_rxCount = 0;
static uint32_t s_noAckCount = 0;
static uint32_t s_badFrameCount = 0;
static uint32_t s_dupCount = 0;

static uint32_t s_lastHealthMs = 0;

// (src, seq) pairs already delivered, newest last.
static uint32_t s_seen[DEDUP_HISTORY] = {0};
static uint8_t s_seenNext = 0;

static uint8_t s_lastTxBuf[FRAME_MAX_SIZE];
static uint8_t s_lastTxLen = 0;
static uint8_t s_lastRxBuf[FRAME_MAX_SIZE];
static uint8_t s_lastRxLen = 0;

// --- helpers --------------------------------------------------------------

static bool alreadySeen(uint16_t src, uint16_t seq) {
  const uint32_t key = ((uint32_t)src << 16) | seq;
  for (uint8_t i = 0; i < DEDUP_HISTORY; i++)
    if (s_seen[i] == key) return true;

  s_seen[s_seenNext] = key;
  s_seenNext = (uint8_t)((s_seenNext + 1u) % DEDUP_HISTORY);
  return false;
}

static void rememberTx(const uint8_t *buf, uint8_t len) {
  s_lastTxLen = len;
  memcpy(s_lastTxBuf, buf, len);
}

static void rememberRx(const uint8_t *buf, uint8_t len) {
  s_lastRxLen = len;
  memcpy(s_lastRxBuf, buf, len);
}

// Puts one already-encoded frame on the air. endPacket() blocks until the last
// symbol has left, then the radio goes straight back to listening: a receiver
// that is not listening is the commonest way to lose an ACK.
static bool transmitRaw(const uint8_t *buf, uint8_t len) {
  if (!busTake(pdMS_TO_TICKS(500))) return false;

  bool ok = false;
  if (LoRa.beginPacket() == 1) {
    LoRa.write(buf, len);
    ok = (LoRa.endPacket() == 1);
    LoRa.receive();
  }
  busGive();

  if (ok) {
    s_txCount++;
    rememberTx(buf, len);
  }
  return ok;
}

static bool encodeAndSend(uint8_t type, uint16_t seq, const uint8_t *payload,
                          uint8_t payloadLen) {
  frame_t f;
  memset(&f, 0, sizeof(f));
  f.ver = PROTO_VERSION;
  f.type = type;
  f.src = config().node_id;
  f.seq = seq;
  f.len = payloadLen;
  if (payloadLen) memcpy(f.payload, payload, payloadLen);

  uint8_t buf[FRAME_MAX_SIZE];
  const int n = frame_encode(&f, buf, sizeof(buf));
  if (n <= 0) return false;

  return transmitRaw(buf, (uint8_t)n);
}

static void sendAck(uint16_t forSeq) {
  const payload_ack_t ack = {forSeq, (int8_t)constrain(s_lastRssi, -128, 127),
                             (int8_t)constrain((int)(s_lastSnr * 4.0f), -128, 127)};
  uint8_t payload[PAYLOAD_ACK_SIZE];
  payload_ack_put(payload, sizeof(payload), &ack);

  // An ACK carries its own sequence number so it can be traced in a log, but
  // nothing waits for an ACK of an ACK.
  encodeAndSend(MSG_ACK, s_txSeq++, payload, PAYLOAD_ACK_SIZE);
}

// --- receive --------------------------------------------------------------

// Returns true when an ACK for `waitingForSeq` was seen. Everything else that
// arrives is handled normally on the way past: a node that ignores traffic
// while waiting for an ACK drops exactly the frames it is busiest receiving.
static bool pumpReceive(uint16_t waitingForSeq, bool waiting) {
  bool ackSeen = false;

  for (;;) {
    // The bus is held only long enough to drain one packet out of the modem.
    // Decoding and dispatch happen outside the lock, so a slow OLED redraw
    // triggered from here can never block a probe or a transmit.
    if (!busTake(pdMS_TO_TICKS(100))) return ackSeen;

    uint8_t buf[FRAME_MAX_SIZE];
    uint8_t len = 0;
    if (LoRa.parsePacket() > 0) {
      while (LoRa.available() && len < sizeof(buf)) buf[len++] = (uint8_t)LoRa.read();
      s_lastRssi = LoRa.packetRssi();
      s_lastSnr = LoRa.packetSnr();
    }
    busGive();

    if (len == 0) break;
    rememberRx(buf, len);

    frame_t f;
    const frame_result_t r = frame_decode(buf, len, &f);
    if (r != FRAME_OK) {
      // Not ours, or corrupted past the CRC's ability to hide it. Either way
      // it is counted and dropped, never handed upwards.
      s_badFrameCount++;
      LOG_W(TAG_RADIO, E_RX_BAD_FRAME, (uint32_t)r);
      continue;
    }

    switch (f.type) {
      case MSG_SYMBOL: {
        payload_symbol_t p;
        if (payload_symbol_get(f.payload, f.len, &p) == 0) break;

        // ACK first, dedup second: a duplicate means our previous ACK never
        // arrived, so the sender needs another one even though we have already
        // shown the symbol.
        sendAck(f.seq);

        if (alreadySeen(f.src, f.seq)) {
          s_dupCount++;
          LOG_D(TAG_RADIO, E_RX_DUP, ((uint32_t)f.src << 16) | f.seq);
          break;
        }

        s_rxCount++;
        LOG_I(TAG_RADIO, E_RX, ((uint32_t)f.src << 16) | f.seq);
        uiPostSymbolReceived((char)p.symbol, millis());
        toneRequest((char)p.symbol);
        break;
      }

      case MSG_ACK: {
        payload_ack_t a;
        if (payload_ack_get(f.payload, f.len, &a) == 0) break;
        LOG_D(TAG_RADIO, E_ACK_RX, (uint32_t)(int32_t)a.rssi);
        if (waiting && a.ack_seq == waitingForSeq) ackSeen = true;
        break;
      }

      case MSG_HEALTH: {
        payload_health_t h;
        if (payload_health_get(f.payload, f.len, &h) == 0) break;
        // A peer's health is worth a log line: it is how a node notices that
        // its neighbour is running an older build or has a POST bit set.
        LOG_I(TAG_RADIO, E_HEALTH_TX, ((uint32_t)f.src << 16) | h.post_mask);
        break;
      }

      default:
        break;
    }
  }

  return ackSeen;
}

// --- transmit with retries ------------------------------------------------

static void transmitSymbol(char symbol, const EventStamp &stamp) {
  const uint16_t seq = s_txSeq++;

// LVL_DEBUG is an enum, so the preprocessor cannot see it; the literal is what
// keeps this measurement out of the field image entirely, variable included.
#if LOG_COMPILE_LEVEL >= 4
  const uint32_t startedAtMs = millis();
#endif

  const payload_symbol_t p = {(uint8_t)symbol, stamp.keyedAtMs};
  uint8_t payload[PAYLOAD_SYMBOL_SIZE];
  payload_symbol_put(payload, sizeof(payload), &p);

  const uint8_t attempts = (uint8_t)(config().ack_retries + 1u);
  const uint32_t timeoutMs = config().ack_timeout_ms;
  bool acked = false;

  for (uint8_t attempt = 0; attempt < attempts && !acked; attempt++) {
    if (!encodeAndSend(MSG_SYMBOL, seq, payload, PAYLOAD_SYMBOL_SIZE)) break;
    LOG_D(TAG_RADIO, E_TX, seq);

    const uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
      if (pumpReceive(seq, /*waiting=*/true)) {
        acked = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  if (!acked) {
    s_noAckCount++;
    // The link has degraded but the node is fine. WARN, not ERROR: it still
    // does its job, just worse than it should.
    LOG_W(TAG_RADIO, E_TX_NOACK, seq);
  }

  uiPostSymbolSent(symbol, stamp);
  toneRequest(symbol);

#if LOG_COMPILE_LEVEL >= 4
  // Whole-transaction time, retries included. This is the number that feeds a
  // duty-cycle calculation, so it is worth measuring rather than assuming.
  LOG_D(TAG_RADIO, E_TX_AIRTIME, millis() - startedAtMs);
#endif
}

static void transmitHealth() {
  const payload_health_t h = healthSnapshot();
  uint8_t payload[PAYLOAD_HEALTH_SIZE];
  payload_health_put(payload, sizeof(payload), &h);

  // Fire and forget. Health is a heartbeat: a missing one is itself the
  // signal, so retrying would only spend airtime hiding it.
  encodeAndSend(MSG_HEALTH, s_txSeq++, payload, PAYLOAD_HEALTH_SIZE);
  LOG_I(TAG_RADIO, E_HEALTH_TX, h.post_mask);
  s_lastHealthMs = millis();
}

static void radioTask(void * /*arg*/) {
  s_lastHealthMs = millis();

  for (;;) {
    RadioRequest request;
    if (xQueueReceive(radioQueue, &request, pdMS_TO_TICKS(RADIO_POLL_MS)) == pdTRUE) {
      if (request.kind == RadioRequestKind::Symbol)
        transmitSymbol(request.symbol, request.stamp);
      else
        transmitHealth();
    }

    pumpReceive(0, /*waiting=*/false);

    const uint32_t periodMs = (uint32_t)config().health_period_s * 1000u;
    if (millis() - s_lastHealthMs >= periodMs) transmitHealth();
  }
}

// --- lifecycle ------------------------------------------------------------

bool radioStandDown() {
  if (!busTake(pdMS_TO_TICKS(500))) return false;
  LoRa.sleep();
  busGive();
  return true;
}

bool radioStandUp() {
  if (!busTake(pdMS_TO_TICKS(500))) return false;
  LoRa.receive();
  busGive();
  return true;
}

bool radioProbeChip() {
  if (!busTake(pdMS_TO_TICKS(500))) return false;

  // An explicit transaction, because a bare SPI.transfer() would run at
  // whatever clock and mode the bus was last left in.
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(LORA_CS, LOW);
  SPI.transfer(SX1276_REG_VERSION & 0x7F);  // MSB clear = read
  const uint8_t version = SPI.transfer(0x00);
  digitalWrite(LORA_CS, HIGH);
  SPI.endTransaction();

  busGive();
  return version == SX1276_VERSION_ID;
}

bool radioBegin() {
  const config_t &cfg = config();

  if (!s_busMutex) s_busMutex = xSemaphoreCreateMutex();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin((long)cfg.freq_hz)) return false;

  // None of these were set before, which left the library's defaults in place
  // - including sync word 0x12, the stock value every other off-the-shelf node
  // also uses, so a stranger's traffic was heard and rendered as ours.
  LoRa.setSpreadingFactor(cfg.spreading);
  LoRa.setCodingRate4(cfg.coding_rate);
  LoRa.setSyncWord(cfg.sync_word);
  LoRa.setTxPower(cfg.tx_power, PA_OUTPUT_PA_BOOST_PIN);

  // The chip's own payload CRC, on top of the frame's CRC-16. It costs two
  // bytes of airtime and rejects corruption before the packet is ever handed
  // to the parser.
  LoRa.enableCrc();

  LoRa.receive();
  LOG_I(TAG_RADIO, E_RADIO_READY, cfg.freq_hz / 1000u);
  return true;
}

bool radioBeginWithRetries(uint8_t attempts) {
  for (uint8_t i = 0; i < attempts; i++) {
    if (radioBegin()) return true;
    LOG_E(TAG_RADIO, E_RADIO_INIT_FAIL, i + 1u);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return false;
}

bool radioTaskStart(UBaseType_t priority, BaseType_t core) {
  radioQueue = xQueueCreate(RADIO_QUEUE_LENGTH, sizeof(RadioRequest));
  if (!radioQueue) return false;

  return xTaskCreatePinnedToCore(radioTask, "radio", 4096, nullptr, priority,
                                 &radioTaskHandle, core) == pdPASS;
}

bool radioSendSymbol(char symbol, const EventStamp &stamp, TickType_t timeout) {
  if (!radioQueue) return false;
  const RadioRequest request{RadioRequestKind::Symbol, symbol, stamp};
  const bool queued = (xQueueSend(radioQueue, &request, timeout) == pdTRUE);
  if (!queued) LOG_W(TAG_RADIO, E_QUEUE_FULL, 0);
  return queued;
}

bool radioRequestHealth() {
  if (!radioQueue) return false;
  const RadioRequest request{RadioRequestKind::Health, 0, EventStamp{0, 0}};
  return xQueueSend(radioQueue, &request, 0) == pdTRUE;
}

int radioLastRssi() { return s_lastRssi; }

void radioPrintStats(Print &out) {
  const config_t &cfg = config();
  out.printf("freq      %lu Hz\n", (unsigned long)cfg.freq_hz);
  out.printf("sf        %u   cr 4/%u   sync 0x%02X   tx %u dBm\n", cfg.spreading,
             cfg.coding_rate, cfg.sync_word, cfg.tx_power);
  out.printf("tx        %lu   rx %lu\n", (unsigned long)s_txCount,
             (unsigned long)s_rxCount);
  out.printf("no-ack    %lu   bad frames %lu   dups %lu\n",
             (unsigned long)s_noAckCount, (unsigned long)s_badFrameCount,
             (unsigned long)s_dupCount);
  out.printf("last rssi %d dBm   snr %.1f dB\n", s_lastRssi, (double)s_lastSnr);
}

static void printHex(Print &out, const char *label, const uint8_t *buf, uint8_t len) {
  if (len == 0) {
    out.printf("%s  (none yet)\n", label);
    return;
  }

  out.printf("%s  %u bytes\n  ", label, len);
  for (uint8_t i = 0; i < len; i++) out.printf("%02X ", buf[i]);
  out.println();

  frame_t f;
  if (frame_decode(buf, len, &f) != FRAME_OK) {
    out.println("  (does not decode)");
    return;
  }
  out.printf("  ver %u  type 0x%02X %s  len %u  src %u  seq %u\n", f.ver, f.type,
             frame_type_name(f.type), f.len, f.src, f.seq);
}

void radioPrintLastFrames(Print &out) {
  printHex(out, "tx", s_lastTxBuf, s_lastTxLen);
  printHex(out, "rx", s_lastRxBuf, s_lastRxLen);
}
