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

## 9 · Uplink and enrolment

**What** A node that has never been seen before joins the fleet on its own, and
what it reports is what it actually is.

**How** A factory-fresh board (`pio run -t erase` first) on the field image,
within range of the commissioning network. Then:
```
> net set key <the fleet api key>
> net report
> net
> version
```
Then open the node's page on the fleet site.

**Pass**
- `net` shows `link up` with a real IPv4 address, `clock synced (UTC)`, and
  `last ok` under 60 s.
- The device appears on the site within one report period, with **no manual
  enrolment step** — the serial on the site matches the serial `version` prints,
  character for character.
- The site's firmware version, git hash, build type and config version match
  what `version` prints on the node.
- The site's POST mask equals the mask `self-test` prints.
- Battery on the site matches `health` to within 0.1 V. If the site shows 0.0 V,
  the node's ADC self-test failed — that is a *different* finding from a flat
  battery, and the site renders them differently on purpose.

---

## 10 · Log records reach the site, in order and without gaps

**What** The ring log is readable from a desk, which is the whole point of
having one on a node under a lock.

**How** With the node reporting, generate records with a known shape:
```
> log clear
> config set tx_power 15
> config set tx_power 14
> self-test
> net report
```

**Pass**
- The site's log page for that device shows the `cfg_changed`, `cfg_saved` and
  `post_mask` records within one report period.
- They are decoded to text, not shown as bare numbers — `cfg_saved` reads
  `slot B seq 18`, not `16777234`. A number here means the site's dictionary and
  the firmware's `LogCode` enum have drifted apart.
- The order matches `log dump` on the node.
- `net` shows `logs N sent, 0 lost to ring wrap`.
- Pull the network for 10 minutes, then restore it: the records generated while
  it was down arrive on the next check-in, and `lost to ring wrap` is still 0
  (256 records is far more than 10 minutes produces at INFO).

---

## 11 · Over-the-air update, happy path

**What** A node updates itself and keeps the new image.

**How** Two versions are needed. Tag and build `v1.1.0`, upload its
`manifest.json` and `firmware.bin` to the fleet site, assign it to this node.
Then watch the node's serial output and its screen. Do not touch anything.

**Pass**
- The screen shows the update page during the download, carrying **the node
  number, both version numbers, a progress bar and the battery voltage**.
- The log holds `ota_available`, `ota_begin`, `ota_progress` at 10 % intervals
  and `ota_staged`, in that order.
- The node reboots on its own and comes up announcing `THIS IMAGE IS ON TRIAL`,
  with `T` in the screen header.
- Within 10 minutes it logs `ota_confirmed` and the banner reads
  `update confirmed`. `ota` then reports `state idle`.
- `version` shows the new semver **and** the new git hash. Semver alone is not
  proof: it is typed by a human and can be wrong.
- The site shows the node on the new version and no longer out of date.
- **Reboot the node once more.** It comes up on the new version. If it comes up
  on the old one, the confirmation never actually happened and every future
  reboot will revert — this single step is the one most worth doing.

---

## 12 · Over-the-air update, rollback (do this on a sacrificial board)

**What** An image that does not work does not stay. This is the check that
decides whether the whole update mechanism is safe to point at real nodes, and
it is the one that is always skipped.

**How** Build a version that is deliberately broken and give it a real version
number so it is never confused with a good build. Three separate runs, because
they exercise three different layers:

1. **Cannot boot.** Corrupt the image after its SHA-256 is computed, or build
   one that panics in `setup()` before the network task starts.
2. **Boots but cannot report.** Build one with `net set url` pointed at an
   address that does not answer, or block the node at the access point.
3. **Boots but fails its POST.** Ground `LORA_RST` before the node reboots into
   the new image.

**Pass**

Run 1 — the bootloader's layer, with no help from the firmware:
- The node comes back on the **previous** version with no intervention.
- `version` shows the old semver and old hash.

Run 2 — the trial window:
- `ota` counts down: `ON TRIAL, N s left before automatic rollback`.
- After 10 minutes the log holds `ota_rollback` with reason `1` (never checked
  in) and the node reboots onto the previous version.
- `ota` on the recovered node prints `blocked <the bad version>`.
- Restore the network and wait a full report period: the node does **not**
  install that version again, and logs `ota_blocked`. This is what stops a bad
  release from flattening every battery in the fleet overnight.

Run 3 — the POST layer:
- `ota_rollback` with reason `2` within one report period, not ten minutes.
- Now repeat with the radio *already* dead before the update: the node must
  **not** roll back, because that failure is not a regression. `ota` explains
  itself with `baseline critical POST 0x0008 was already failing`.

**Also record** what a bad image costs: measure the time from `ota_begin` to a
recovered node on the old version, and the battery drop across it. That number
is what decides how low `ota_vbat_min_mv` can safely go.

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
| 9 | Uplink and enrolment | | | |
| 10 | Logs reach the site | | | |
| 11 | OTA happy path | | | |
| 12 | OTA rollback ×3 | | | |

---

## Known gaps

Two rows above test something the firmware does not yet fully provide, and
saying so is cheaper than discovering it during the run:

- **Check 8** asks for free heap, which no command currently prints. Either add
  one before the soak, or read it from the `log dump` timestamps and accept a
  weaker criterion for this run.
- **Check 12** needs a board you are willing to lose. Everything in it is
  designed not to brick a node, but that is exactly the claim being tested, and
  a claim you test on a production node is a claim you have already trusted.
- **Check 11** needs two real releases. Building `v1.1.0` as a re-tag of
  identical code is not sufficient — the git hash has to differ, or `version`
  cannot prove which image is actually running.
- **Check 1** assumes the reboot counter is exact. It is stored in NVS and
  mirrored in RTC memory, and a power cut during the counter's own write could
  lose one increment. If the count is off by one over 20 cycles, that is the
  reason — off by more is a real finding.
