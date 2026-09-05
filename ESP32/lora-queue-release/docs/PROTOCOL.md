# LQ-MORSE protocol specification

Protocol version **1**. Normative field layout: [`lib/proto/frame.h`](../lib/proto/frame.h).

This document and the code are kept in step by a test: `test_frame_golden_symbol`
in [`test/test_native/test_main.cpp`](../test/test_native/test_main.cpp) asserts
that `frame_encode()` produces exactly the bytes printed in section 6. If the
codec changes and this document is not updated, the build fails.

---

## 1. Transport and frame structure

### Physical layer

| Parameter | Value | Where it comes from |
|---|---|---|
| Band | EU868, default 868.000 MHz | `config freq_hz`, range 863–870 MHz |
| Modulation | LoRa (SX1276) | fixed |
| Spreading factor | SF7 | `config spreading`, 7–12 |
| Bandwidth | 125 kHz | library default, not currently configurable |
| Coding rate | 4/5 | `config coding_rate`, 5–8 |
| Sync word | `0x2B` | `config sync_word` |
| Preamble | 8 symbols | library default |
| TX power | 14 dBm, PA_BOOST | `config tx_power`, 2–20 dBm |
| Header | explicit | library default |
| PHY CRC | enabled | `LoRa.enableCrc()` |

The sync word is deliberately **not** `0x12`. That is the value every
off-the-shelf `sandeepmistry/LoRa` node ships with, and while it was in use any
stranger's traffic in range was received and rendered as our own symbols.

### Framing

The frame is transport-agnostic. Over LoRa the PHY already provides a preamble,
a sync word, an explicit length header and its own CRC, so `SYNC` and `LEN` are
strictly redundant there. They are kept because the same bytes are dumped over
UART by the `frames` command and pasted into tickets, where there is no framing
underneath and a reader needs to find the start of a frame and know where it
ends.

There is no COBS or byte stuffing: `LEN` is authoritative and the decoder
requires the received length to match `10 + LEN` exactly. A frame with trailing
bytes is rejected rather than trimmed.

```
+--------+-----+------+-----+---------+---------+-----------+---------+
|  SYNC  | VER | TYPE | LEN |   SRC   |   SEQ   |  PAYLOAD  |  CRC16  |
| 1 byte |  1  |  1   |  1  | 2 (LE)  | 2 (LE)  |  0..48 B  | 2 (LE)  |
+--------+-----+------+-----+---------+---------+-----------+---------+
 \_______________________ CRC16 covers this _______________/
```

Header 8 bytes + CRC 2 bytes = **10 bytes of overhead**.

---

## 2. Field table

| Offset | Size | Field | Type | Byte order | Description |
|---:|---:|---|---|---|---|
| 0 | 1 | `SYNC` | `uint8` | — | Always `0xA5`. |
| 1 | 1 | `VER` | `uint8` | — | Protocol version. Currently `0x01`. A receiver rejects any other value. |
| 2 | 1 | `TYPE` | `uint8` | — | Message type; see section 3. |
| 3 | 1 | `LEN` | `uint8` | — | Payload length in bytes, `0..48`. |
| 4 | 2 | `SRC` | `uint16` | little-endian | Sender's `node_id`, `1..999`. |
| 6 | 2 | `SEQ` | `uint16` | little-endian | Per-source counter, increments once per transmitted frame, wraps at `0xFFFF`. |
| 8 | `LEN` | `PAYLOAD` | — | little-endian | Structure per `TYPE`. |
| 8+`LEN` | 2 | `CRC16` | `uint16` | little-endian | CRC-16/CCITT-FALSE over bytes `[0 .. 8+LEN-1]`. |

**Every multi-byte field is little-endian**, in the frame header and in every
payload. The codec never uses `memcpy` of a struct onto the wire: each field is
written byte by byte, so the encoding is identical on the ESP32 and on the host
that runs the tests.

### CRC-16 parameters

| | |
|---|---|
| Name | CRC-16/CCITT-FALSE |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input reflected | no |
| Output reflected | no |
| Final XOR | `0x0000` |
| Check (`"123456789"`) | `0x29B1` |

The CRC covers the **header as well as the payload**. Covering only the payload
would let `SRC` be swapped without touching the useful data, which is exactly
the substitution worth preventing.

The CRC is not a signature. The algorithm is public, so anyone who changes a
byte can recompute it. It catches accidental corruption in the channel and
nothing more; authenticity would need a MAC over a shared key, which this
version does not have (see section 7).

---

## 3. Message types and payloads

| Code | Name | Payload size | Direction | ACKed |
|---|---|---:|---|---|
| `0x01` | `SYMBOL` | 5 | node → node | yes |
| `0x02` | `ACK` | 4 | node → node | no |
| `0x03` | `HEALTH` | 12 | node → gateway/peer | no |
| `0x04`–`0x3F` | reserved | — | — | — |
| `0x40`–`0x7F` | vendor / experimental | — | — | — |
| `0x80`–`0xFF` | reserved | — | — | — |

Codes are handed out with room to grow rather than assigned one after another
until they run out. A receiver that meets an unknown code returns
`FRAME_ERR_TYPE` but has already parsed `SRC` and `SEQ`, so the event is still
worth logging.

### `0x01 SYMBOL`

| Offset | Size | Field | Type | Description |
|---:|---:|---|---|---|
| 0 | 1 | `symbol` | `uint8` | ASCII `'.'` (`0x2E`) or `'-'` (`0x2D`). |
| 1 | 4 | `key_ms` | `uint32` LE | Sender's monotonic clock when the key gesture was classified. |

`key_ms` is monotonic milliseconds since the sender booted, not wall-clock time.
Nodes have no RTC; absolute time is assigned by the gateway on receipt.

### `0x02 ACK`

| Offset | Size | Field | Type | Description |
|---:|---:|---|---|---|
| 0 | 2 | `ack_seq` | `uint16` LE | `SEQ` of the frame being acknowledged. |
| 2 | 1 | `rssi` | `int8` | dBm at which the acknowledged frame was received. |
| 3 | 1 | `snr_q2` | `int8` | SNR in quarter-dB (divide by 4 for dB). |

The ACK carries link quality back to the sender, which is how a degrading link
becomes visible long before packets start disappearing.

### `0x03 HEALTH`

| Offset | Size | Field | Type | Description |
|---:|---:|---|---|---|
| 0 | 2 | `fw_hash` | `uint16` LE | Low 16 bits of the build's git hash. Shows at a glance who has not updated. |
| 2 | 3 | `uptime_s` | `uint24` LE | Seconds since boot. Wraps at 194 days. |
| 5 | 1 | `reboots` | `uint8` | Total boots, saturating at 255. A silent reboot is invisible without this. |
| 6 | 1 | `last_crash` | `uint8` | `esp_reset_reason()` as a number, decoded via `log_dict.csv`. |
| 7 | 1 | `vbat_dv` | `uint8` | Battery in tenths of a volt, already calibrated. `0` means the reading is untrusted. |
| 8 | 2 | `last_rssi` | `int16` LE | RSSI of the last received frame, dBm. |
| 10 | 2 | `post_mask` | `uint16` LE | POST result, one bit per block. See below. |

`post_mask` bits:

| Bit | Block | Critical |
|---:|---|---|
| 0 | `power` | yes |
| 1 | `nvs` | no |
| 2 | `adc` | no |
| 3 | `radio` | yes |
| 4 | `display` | no |
| 5 | `button` | no |

Twelve bytes against a drive out is the cheapest diagnostic there is, and the
only one that works while the node is still alive.

---

## 4. Versioning rules

`VER` is the protocol's version and is independent of the firmware version.
Firmware 1.4.2 may perfectly well speak protocol 1.

### Allowed within `VER = 1`

- Assigning a new `TYPE` code from the reserved ranges. Old receivers report
  `FRAME_ERR_TYPE`, log `SRC`/`SEQ`, and drop the frame — they do not
  malfunction.
- Adding fields to the **end** of a payload whose `TYPE` is not yet in use by
  any deployed node.
- Changing any value that is already a config field (frequency, SF, coding
  rate, sync word, TX power, timeouts, retry count). These are settings, not
  protocol.

### Requires `VER = 2`

- Any change to the fixed 8-byte header: reordering, resizing, changing the
  meaning of a field, or changing an endianness.
- Any change to the layout or meaning of an **existing** payload, including
  appending to one that is already deployed. A receiver has no way to tell a
  longer new payload from a corrupted old one.
- Changing the CRC algorithm or its coverage.
- Changing `SYNC`.

### Receiver behaviour

A frame whose `VER` does not match the receiver's own is rejected with
`FRAME_ERR_VERSION` before its payload is touched. There is no attempt to
interpret a newer protocol partially: guessing at fields that are not understood
corrupts them silently, and silent corruption is worse than a dropped frame.

---

## 5. Errors, ACK, retry and timeouts

### Reject reasons

| Result | Meaning | Counted as |
|---|---|---|
| `FRAME_ERR_SHORT` | Fewer than 10 bytes arrived. | bad frame |
| `FRAME_ERR_SYNC` | Byte 0 is not `0xA5`. | bad frame |
| `FRAME_ERR_VERSION` | `VER` is not 1. | bad frame |
| `FRAME_ERR_LENGTH` | `LEN` > 48, or `10 + LEN` ≠ bytes received. | bad frame |
| `FRAME_ERR_CRC` | Body intact, CRC does not match. | bad frame |
| `FRAME_ERR_TYPE` | Well-formed, `TYPE` unknown to this build. | bad frame |

Every rejection increments the bad-frame counter (`radio` command) and logs
`E_RX_BAD_FRAME` at WARN with the reason code.

### ACK and retry

| Parameter | Default | Range | Config key |
|---|---:|---|---|
| ACK timeout | 600 ms | 100–5000 | `ack_timeout_ms` |
| Retries after the first attempt | 3 | 0–5 | `ack_retries` |
| Maximum attempts | 4 | 1–6 | — |
| Worst-case transaction | 2.4 s | — | `(retries+1) × timeout` |

1. The sender transmits a `SYMBOL` frame and returns to receive.
2. It waits up to `ack_timeout_ms` for an `ACK` whose `ack_seq` matches. While
   waiting it keeps processing everything else that arrives — a node that goes
   deaf while waiting for an ACK drops exactly the frames it is busiest
   receiving.
3. On timeout it retransmits **the same `SEQ`**, up to `ack_retries` more times.
4. After the last attempt it logs `E_TX_NOACK` at WARN and gives up. WARN, not
   ERROR: the node is fine, the link is worse than it should be.

`HEALTH` is fire-and-forget. A missing heartbeat is itself the signal; retrying
would spend airtime to hide it.

### Duplicate suppression

The receiver keeps the last 8 `(SRC, SEQ)` pairs. A repeat is ACKed again but
delivered only once — a duplicate means the previous ACK was lost, so the sender
still needs one, but the operator must not see the symbol twice.

Eight entries is deliberate: a duplicate arriving after eight other frames is
not a retry, it is a different problem.

---

## 6. A real frame in hex, byte by byte

Node 1 sends a dash, sequence 7, keyed at 123456 ms after boot.

```
A5 01 01 05 01 00 07 00 2D 40 E2 01 00 13 A1
```

15 bytes total: 8 header + 5 payload + 2 CRC.

| Offset | Bytes | Field | Value | Reading |
|---:|---|---|---|---|
| 0 | `A5` | `SYNC` | 0xA5 | Frame start. |
| 1 | `01` | `VER` | 1 | Protocol 1. |
| 2 | `01` | `TYPE` | 1 | `SYMBOL`. |
| 3 | `05` | `LEN` | 5 | 5 payload bytes → 15 total. |
| 4–5 | `01 00` | `SRC` | 1 | LE: `0x0001`. Node 1. |
| 6–7 | `07 00` | `SEQ` | 7 | LE: `0x0007`. |
| 8 | `2D` | `symbol` | `'-'` | ASCII 0x2D, a dash. |
| 9–12 | `40 E2 01 00` | `key_ms` | 123456 | LE: `0x0001E240` = 123456 ms. |
| 13–14 | `13 A1` | `CRC16` | 0xA113 | LE: CRC-16/CCITT-FALSE over bytes 0–12. |

The CRC input is the first 13 bytes:
`A5 01 01 05 01 00 07 00 2D 40 E2 01 00` → `0xA113`, stored low byte first.

### The same node's health frame

Node 1, sequence 42, firmware hash `0x9A1C`, up 123456 s, 3 reboots, no crash,
3.9 V, last RSSI −97 dBm, POST all clear.

```
A5 01 03 0C 01 00 2A 00 1C 9A 40 E2 01 03 00 27 9F FF 00 00 62 BA
```

22 bytes: 8 header + 12 payload + 2 CRC.

| Offset | Bytes | Field | Value |
|---:|---|---|---|
| 0–7 | `A5 01 03 0C 01 00 2A 00` | header | `SYNC`, `VER` 1, `TYPE` 3 (`HEALTH`), `LEN` 12, `SRC` 1, `SEQ` 42 |
| 8–9 | `1C 9A` | `fw_hash` | `0x9A1C` |
| 10–12 | `40 E2 01` | `uptime_s` | 123456 s (1 day 10:17:36) |
| 13 | `03` | `reboots` | 3 |
| 14 | `00` | `last_crash` | 0 |
| 15 | `27` | `vbat_dv` | 39 → 3.9 V |
| 16–17 | `9F FF` | `last_rssi` | `0xFF9F` = −97 dBm |
| 18–19 | `00 00` | `post_mask` | 0x0000, everything passed |
| 20–21 | `62 BA` | `CRC16` | 0xBA62 |

Both frames can be reproduced on a live board with the `frames` command, which
prints the last transmitted and last received frame in hex and decodes the
header.

---

## 7. Limits

### Sizes

| | |
|---|---|
| Maximum payload (MTU) | 48 bytes |
| Maximum frame | 58 bytes |
| Header overhead | 10 bytes (17 % of a maximum frame, 67 % of a `SYMBOL` frame) |

The MTU is set well below the SX1276's 255-byte limit on purpose: at SF12 a
255-byte frame occupies the channel for several seconds, which is unusable
under a 1 % duty cycle.

### Airtime and rate

Time on air for a 15-byte `SYMBOL` frame, BW 125 kHz, CR 4/5, 8-symbol
preamble, explicit header, CRC on:

| SF | Airtime | Frames/hour at 1 % duty cycle |
|---:|---:|---:|
| 7 | ≈ 51 ms | ≈ 700 |
| 9 | ≈ 165 ms | ≈ 218 |
| 12 | ≈ 1155 ms | ≈ 31 |

Every keyed symbol may cost up to 4 transmissions (1 attempt + 3 retries) plus
an ACK from the peer, so budget roughly 5 frames per symbol in the worst case.

### Regulatory

| | |
|---|---|
| Band | EU868 sub-band g1, 868.0–868.6 MHz |
| Duty cycle | ≤ 1 % per hour per device |
| ERP limit | ≤ 14 dBm (25 mW) |
| Default TX power | 14 dBm — at the limit before antenna gain |

`tx_power` can be configured up to 20 dBm because the chip supports it, not
because it is legal everywhere. Above 14 dBm ERP the operator is responsible for
staying within local regulations.

### Not enforced by the firmware

**Rate limiting is not implemented.** A user keying continuously can exceed the
1 % duty cycle. The `health_period_s` timer is bounded (10–3600 s) but symbol
traffic is not. Before any real deployment this needs a transmit budget that
tracks airtime over a rolling hour and refuses to transmit once the allowance is
spent.

**Listen-before-talk is not implemented.** The node transmits without checking
whether the channel is busy.

### Security

This version has **no authentication and no encryption**. `SRC` can be forged
freely and any frame can be replayed. The CRC detects accidental corruption
only.

The next step, when it is needed, is a MAC over a shared key plus a nonce —
about 14 extra bytes on the frame. `SRC` and `SEQ` must stay in clear for
routing and duplicate suppression; the MAC must cover the whole frame including
the header, or `SRC` can be swapped without touching the payload; and the nonce
is what stops yesterday's recorded frame being replayed today, which needs no
key at all.
