# Field checklist

Run this before a node is deployed. Every criterion is a number or an exact
string that can be read off the serial monitor or the screen — never "works
normally".

Unless a row says otherwise, run it on the **field** image. Testing a dev build
and shipping a field build is the classic way to be surprised.

```bash
~/.platformio/penv/Scripts/pio.exe run -e field -t upload --upload-port COM5
```

Record the results in the table at the bottom.

---

## 1 · Cold start ×20

**What** The node comes up the same way every time, with no silent reboots.

**How** Disconnect USB (and the battery, if fitted) completely. Wait 5 s.
Reconnect. Repeat 20 times. After the last cycle run `version` and
`log dump 50`.

**Pass**
- 20 of 20 boots reach `post 0x0000 OK`.
- `reboots` in `version` has increased by exactly 20 over the run — no more.
- `last:` reads `POWERON` every time, never `PANIC`, `TASK_WDT` or `BROWNOUT`.
- `abnormal streak` is absent from the `version` output.
- `log dump 50` contains zero `PANIC` and zero `ERROR` records.

---

## 2 · Empty NVS

**What** A node with nothing stored comes up on the firmware's defaults rather
than on zeros, and says so.

**How**
```bash
~/.platformio/penv/Scripts/pio.exe run -e field -t erase --upload-port COM5
~/.platformio/penv/Scripts/pio.exe run -e field -t upload --upload-port COM5
```
Then `version`, `config get`, `log dump 30`.

**Pass**
- The node reaches its shell prompt within 5 s of power-up.
- `post` shows bit 1 (`nvs`) set — mask `0x0002` — on the first boot only.
- The log contains `cfg_defaults` at WARN.
- `config get` shows every documented default: `tx_power 14`, `spreading 7`,
  `coding_rate 5`, `sync_word 43`, `freq_hz 868000000`, `health_period_s 60`,
  `ack_timeout_ms 600`, `ack_retries 3`, `vbat_min_mv 3300`, `log_level 3`.
- `cfg_version` is 2 and `cfg_seq` is 0 until the first write.
- `serial` in `version` is unchanged from before the erase **if** the node was
  provisioned from the factory image. An erase does clear the calibration
  namespace, so an unprovisioned node falls back to its MAC-derived name.

---

## 3 · Config survives a power cut mid-write

**What** A write interrupted by a power cut leaves the previous value in place,
never a third value and never defaults.

**How** `scripts/powercut.ps1 -Port COM5 -Cycles 20`, or manually per
[POWER_CUT_EXPERIMENT.md](POWER_CUT_EXPERIMENT.md).

**Pass**
- All 20 cycles yield either the old value with the old `cfg_seq`, or the new
  value with `cfg_seq + 1`. No cycle yields anything else.
- Zero cycles come up on defaults (`cfg_defaults` never appears in the log).
- Every cycle where the cut landed inside a write shows `cfg_slot_bad` at WARN
  in the log, naming the slot that was rejected.
- `config get` after every cycle shows a `cfg_seq` that never decreases.

---

## 4 · Config migration v1 → v2

**What** A node holding a config from an older firmware walks it forward and
keeps the values an operator set.

**How** Needs the **dev** image, which is the only one that carries the seed
command.
```
> config seed_v1
> reboot
> config get
> log dump 20
```

**Pass**
- The log contains `cfg_migrated` at INFO.
- `config get` shows `tx_power 11` and `health_period_s 120` — the v1 values,
  carried across by name.
- The five fields v1 did not have are at their code defaults:
  `coding_rate 5`, `sync_word 43`, `ack_timeout_ms 600`, `ack_retries 3`,
  `vbat_min_mv 3300`. None of them is 0.
- `cfg_version` reads 2.
- `version` shows `(migrated from v1)`.
- A second `reboot` does **not** log `cfg_migrated` again: the walk happened
  once and was written back.

---

## 5 · Link with no peer

**What** A node whose partner is switched off keeps working and says the link
is down, rather than blocking or rebooting.

**How** Power node 1 with node 2 switched off. Key 10 symbols. Then `radio`,
`version`, `log dump 40`.

**Pass**
- `log dump` shows exactly 10 `tx_noack` records at WARN.
- `radio` reports `no-ack 10` and `tx 40` — 10 symbols × (1 + `ack_retries` 3)
  attempts each.
- Each symbol takes ≈ 2.4 s to give up (4 × `ack_timeout_ms` 600).
- The shell answers `version` throughout.
- `reboots` is unchanged from before the test.
- Zero `ERROR` records: a dead link is a WARN, the node is doing its job.

---

## 6 · Radio held in reset

**What** A node with a dead radio still comes up, reports which block failed,
and stays diagnosable.

**How** Power off. Jumper `LORA_RST` (GPIO23) to GND. Power on. Read the OLED,
then `version`, `self-test`, `log dump 30`. Remove the jumper and power-cycle.

**Pass**
- The OLED bottom-right shows `P:008`, and the banner reads `RADIO FAIL`.
- `version` shows `post 0x0008 FAIL`.
- `self-test` shows `bit 3  radio  FAIL` with its reaction text.
- The log holds three `radio_init_fail` records at ERROR, with args 1, 2 and 3.
- `post_critical` appears at ERROR with arg 3.
- The shell answers every command; the node does not reboot-loop.
- After the jumper is removed and power cycled, `post` returns to `0x0000`.

---

## 7 · Low-battery write gate

**What** The node refuses to write flash on a flat pack, and explains why.

**How** Power the board from a bench supply on the battery input at 3.10 V
(USB disconnected). Then:
```
> config get            # note tx_power and cfg_seq
> config set tx_power 17
> config get
```

**Pass**
- The command is refused with `ERR power: vbat 31xx mV < 3300 mV, refusing
  flash write` — the actual measured millivolts, not a placeholder.
- `config get` still shows the old `tx_power` and an **unchanged** `cfg_seq`.
- The log holds `lowbat_write_blocked` at WARN with the millivolts as its arg.
- Raise the supply to 3.9 V and repeat: the same command now succeeds and
  `cfg_seq` increments by 1.

---

## 8 · 24-hour soak

**What** Nothing leaks, nothing overflows and nothing falls over when left
alone.

**How** Two nodes on the field image, in the location they will be deployed in.
Key one symbol per minute from node 1 (a timer or a patient person).
`config set health_period_s 60` on both. Capture the serial output of both for
24 h. At the 1 h mark and at the 24 h mark, record `version` and `radio`.

**Pass**
- `reboots` is identical at 1 h and at 24 h — zero unplanned reboots.
- Zero `ERROR` and zero `PANIC` records over the whole run.
- At least 1380 of the 1440 health frames are received by the peer (≥ 95.8 %).
- `no-ack` is ≤ 5 % of `tx` over the run.
- `post` is `0x0000` at both checkpoints.
- Free heap at 24 h is within 2 KB of the reading at 1 h. (Add a `heap` command
  or read `ESP.getFreeHeap()` — see the note below.)

---

## Recording sheet

| # | Check | Date | Result | Evidence |
|---|---|---|---|---|
| 1 | Cold start ×20 | | | |
| 2 | Empty NVS | | | |
| 3 | Power cut mid-write | | | |
| 4 | Migration v1 → v2 | | | |
| 5 | No peer | | | |
| 6 | Radio in reset | | | |
| 7 | Low-battery gate | | | |
| 8 | 24-hour soak | | | |

---

## Known gaps

Two rows above test something the firmware does not yet fully provide, and
saying so is cheaper than discovering it during the run:

- **Check 8** asks for free heap, which no command currently prints. Either add
  one before the soak, or read it from the `log dump` timestamps and accept a
  weaker criterion for this run.
- **Check 1** assumes the reboot counter is exact. It is stored in NVS and
  mirrored in RTC memory, and a power cut during the counter's own write could
  lose one increment. If the count is off by one over 20 cycles, that is the
  reason — off by more is a real finding.
