# Troubleshooting

Format is fixed: **symptom → how to check → what to do.** No explanations of how
it works inside; that is what the other documents are for.

| Symptom | How to check | What to do |
|---|---|---|
| Node shows nothing on the OLED | `version` over UART still answers? | If yes: `post` bit 4 (`display`) is set — the panel or its I²C wiring. The node is fully usable headless. If no: see the next row. |
| Node completely silent, no UART | Does the USB-serial port enumerate at all? | Port missing → cable or CH9102 driver. Port present but silent → reflash. Do **not** start probing GPIO16: on these PICO-D4 boards it is the embedded-flash chip select and driving it wedges the boot. |
| OLED bottom-right shows `P:008` | `self-test` | Radio failed. Check the antenna is fitted and the `LORA_RST` line is not shorted. Three init attempts already happened; power-cycle once, then replace the board. |
| OLED shows `P:002` after a reflash | `log dump 20`, look for `cfg_defaults` | Expected on the first boot after `-t erase`. Set the node's settings again and confirm `cfg_seq` becomes 1. If it recurs on every boot, NVS is failing — replace the board. |
| `config set` answers `ERR power` | Read the millivolts in the message | Not a bug. The pack is below `vbat_min_mv` and the node refuses to write flash — that refusal is what stops a flat battery bricking it. Charge above 3.4 V and repeat. |
| `config set` answers `ERR range` | The message names the exact bounds | Use a value inside them. The bounds live in the device; a dashboard showing different ones is out of date. |
| `config set` answers `ERR unknown field` | `config get` | The field name changed or never existed. `config get` is the authoritative list. |
| Symbols sent but the peer shows nothing | `radio` on the sender | `no-ack` climbing → the peer is off, out of range, or on different settings. Compare `freq_hz`, `spreading`, `coding_rate` and `sync_word` on both with `config get`. All four must match. |
| Symbols arrive twice | `radio`, check `dups` | `dups` climbing means ACKs are being lost, so the sender retries. The duplicate is suppressed before display. Raise `ack_timeout_ms` if the link is slow (high SF). |
| `radio` shows `bad frames` climbing | `log dump 40`, read the `rx_bad_frame` args | Arg 5 (`FRAME_ERR_CRC`) → interference or a marginal link; check RSSI. Arg 3 (`FRAME_ERR_SYNC`) or 4 (`FRAME_ERR_VERSION`) → someone else's traffic on the same frequency; change `sync_word` on both nodes. |
| `version` shows a growing `reboots` | `version`, read `last:` | `POWERON` → someone is power-cycling it. `TASK_WDT` or `PANIC` → a real fault: capture `log dump 50` and the `.elf`/`.map` of that exact build, then send them to development. Do not change the config to work around it. |
| `version` shows `SAFE MODE` | `version`, read `abnormal streak` | Three or more abnormal boots. The key and the sidetone are off; the shell, radio and telemetry are up on purpose. Capture `log dump 50`, then power-cycle: a clean power-on clears the streak. |
| `version` says the image is `-dirty` | The warning line under the version | This build was made from uncommitted changes and there is no commit to reproduce it from. It must not be deployed. Rebuild from a tagged commit. |
| Node identity unknown, no laptop present | Hold the key for 2 seconds | The screen switches to the identity page: serial, node id, firmware version, git hash, build type, POST mask, uptime, reboots and battery. Hold again to switch back. |
| Two nodes answer to the same id | `version` on each | Both fell back to a MAC-derived id and collided, or one was set by hand. `config set node_id <n>` on one of them; the range is 1–999. |
| Battery reading looks wrong or shows `?` | `self-test`, check bit 0 (`power`) and bit 2 (`adc`) | Both set → the sense divider is missing or the ADC is stuck; the low-power write gate is disabled on purpose so a broken sensor cannot lock you out. Bit 0 only → the pack really is outside 3.0–4.6 V. |
| Settings reverted after a power cut | `config get`, compare `cfg_seq` with what it was | Working as designed: the write was interrupted and the previous complete record was kept. Repeat the change on a stable supply. `log dump` will show `cfg_slot_bad`. |
| `log dump` shows nothing useful | `log level` | The runtime level may be too low. `config set log_level 4` for DEBUG. Note that a **field** image contains no DEBUG or TRACE at all — they are not compiled in — so raising the level above 2 there changes nothing. Use the dev image to investigate. |

---

## What to capture before asking for help

1. `version` — always. It identifies the exact code.
2. `self-test` — the POST mask and which blocks failed.
3. `log dump 50` — the records leading up to the problem.
4. `radio` — link settings and counters.
5. `config get` — the full settings dump.
6. `frames` — the last frame in and out, in hex.

Plus the `firmware.elf` and `firmware.map` of the build named by `version`.
Without them, a program counter from a panic is just a number.
