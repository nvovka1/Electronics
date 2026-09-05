# Breaking it on purpose: power cut during a config write

**Question.** If the power disappears in the middle of saving a setting, what
does the node come up with?

**Required answer.** The previous, complete value. Never half of the new one,
never defaults, never a boot loop.

---

## Why this is not automatic

It is tempting to answer "NVS is transactional, so it is fine". NVS does protect
the integrity of a single key, but that only guarantees the write either landed
or did not. It says nothing about what the application is left holding, and the
common failure is not a torn key — it is a node that comes up with **no valid
config at all** and silently falls back to defaults, losing every setting an
operator ever made.

So the design does not rely on it.

## The mechanism

Two records, `a` and `b`, in the NVS namespace `cfg`. Each carries a magic word,
a monotonic sequence number, the config blob, and a CRC-32 over everything
before it.

```
                    active                    target
                  +--------+                +--------+
   before write   | A seq 16 |  (valid)     | B seq 14 |  (old, valid)
                  +--------+                +--------+

   write          | A seq 16 |  UNTOUCHED   | B  ...   |  <-- power cut lands here
                  +--------+                +--------+

   next boot      | A seq 16 |  chosen      | B  ????  |  CRC fails -> rejected
                  +--------+                +--------+
```

A write **always** targets the slot that is not currently active
([`config.cpp`](../src/core/config.cpp), `persist()`), so a complete valid
record exists at every instant of the write. On load, both slots are read and
validated, and the valid one with the higher sequence number wins
([`cfg_record.cpp`](../lib/cfg/cfg_record.cpp), `cfg_record_pick()`).

This is the OTA two-slot rule applied to settings, and it is the reason the
answer to the question is a mechanism rather than a hope.

The slot-selection logic is covered by host tests, including the exact case this
experiment reproduces —
`test_slot_pick_ignores_the_torn_one` in
[`test/test_native/test_main.cpp`](../test/test_native/test_main.cpp).

---

## Rig

- One LoRa32 board, **USB power only**. Disconnect the battery: with a battery
  fitted, "power off" is a lie and the experiment measures nothing.
- A way to cut the 5 V line: a switched USB hub, an inline switch, or simply
  pulling the cable.
- A serial terminal at 115200.

The board must be on the **dev** or **field** image; both behave identically
here. The `factory` image is not used because a POST failure there stops the
board and would confuse the run.

---

## Procedure, per cycle

1. `config get` — record `tx_power` and `cfg_seq`, and which slot is active.
2. `config set tx_power 17` (alternate with `14` on the next cycle, so the value
   actually changes every time).
3. **Cut the power 10–60 ms after pressing Enter.** Vary the delay across
   cycles so the cut lands at different points in the erase/write/commit
   sequence. This is the part that matters: a cut that always lands in the same
   place tests one code path 20 times.
4. Restore power. Wait for the shell prompt.
5. `config get` — record `tx_power`, `cfg_seq` and the active slot.
6. `log dump 20` — look for `cfg_slot_bad`, which is the visible evidence that a
   torn record was found and rejected.

Repeat 20 times.

`scripts/powercut.ps1` automates steps 1, 2, 5 and 6 and prompts for the manual
cut, so the only thing a person does is flip the switch.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\powercut.ps1 -Port COM5 -Cycles 20
```

---

## What each outcome means

| After restart | Meaning | Verdict |
|---|---|---|
| Old value, old `cfg_seq` | The cut landed before the new record was complete. The torn slot failed its CRC and the old one was kept. | **Pass** |
| New value, `cfg_seq + 1` | The cut landed after the write completed. | **Pass** |
| Old value, and `cfg_slot_bad` in the log | Same as row 1, with the rejection made explicit. | **Pass, and the best evidence** |
| Any third value | A partially written record was accepted. | **Fail** |
| Defaults, `cfg_defaults` in the log | Both slots were invalid at once. | **Fail** |
| Boot loop, or no shell | Something worse than a config problem. | **Fail** |

A run in which no cycle ever produces `cfg_slot_bad` is not a pass — it means
the cuts never actually landed inside a write, and the mechanism has not been
exercised. Shorten the delay and run it again.

---

## Results

Firmware: `_______________` (paste the `version` output)
Date: `___________`  Operator: `___________`

| # | Cut delay (ms) | Before: value / seq / slot | After: value / seq / slot | `cfg_slot_bad`? | Verdict |
|---:|---:|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |
| 4 | | | | | |
| 5 | | | | | |
| 6 | | | | | |
| 7 | | | | | |
| 8 | | | | | |
| 9 | | | | | |
| 10 | | | | | |
| 11 | | | | | |
| 12 | | | | | |
| 13 | | | | | |
| 14 | | | | | |
| 15 | | | | | |
| 16 | | | | | |
| 17 | | | | | |
| 18 | | | | | |
| 19 | | | | | |
| 20 | | | | | |

**Summary**

- Cycles run: `___`
- Cuts that landed inside a write (`cfg_slot_bad` seen): `___`
- Came up on the old value: `___`
- Came up on the new value: `___`
- Came up on anything else: `___` ← must be 0
- Came up on defaults: `___` ← must be 0

### Log excerpt from a caught cycle

Paste at least one `log dump` showing a rejected slot. It should look like:

```
      ts_ms  level tag   code                    arg
         41  WARN  cfg   cfg_slot_bad            1
         43  INFO  cfg   cfg_loaded              268435472
```

`cfg_slot_bad 1` is slot B rejected. `cfg_loaded` packs the slot into the top
byte and the sequence into the rest: `0x10000010` is slot A (`0x10 >> 4`… see
`E_CFG_LOADED` in [`log.h`](../src/core/log.h)) at sequence 16.

---

## Conclusion

Fill in after the run:

> Across `__` power cuts landing inside a config write, the node came up on the
> previous complete value `__` times and on the new value `__` times. It never
> came up on a partially written config or on defaults.
