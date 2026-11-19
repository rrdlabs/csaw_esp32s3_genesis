# CsAW Implementation Progress Ledger

**Purpose:** Chronological, append-only record of implementation work. Every change is versioned in [VERSION.md](../VERSION.md) and appended to [CHANGELOG.md](../CHANGELOG.md) with date, version, summary, **files touched**, and **verification result**. If work is interrupted, resume from the *Current state* block below — do not restart or guess what was done.

**Governing spec:** `AGENTS.md` §§3–11 (workspace root). **Task 1 (`agent-preflight`) is complete.** Implementation is authorized; work proceeds in the work packages below.

---

## Current state (update this block on every increment)

| Field | Value |
| --- | --- |
| Last completed increment | First physical flash attempt recorded; bringup FAIL documented; flash helper fixed (v1.1.1) |
| Current version | `1.1.1` |
| Active work package | WP10 — secondary BSP repository (operator approved creation despite hardware failure) |
| Next concrete action | Finish and push `rrdlabs/csaw-genesis-bsp`, labelled with the hardware FAIL; re-test on a genuinely powered Rev 1 board when available |
| Last verification | Flash PASS (`/tmp/opencode/csaw_flash_genesis.log`, hash verified); boot/bringup FAIL (`/tmp/opencode/csaw_boot_monitor.log`, I²C scan `/tmp/opencode/csaw_probe_monitor.log` found zero devices). Offline build/merge checks from 1.1.0 remain PASS |
| Commands attempted | WP1–WP9 as logged below; WP9 hardware attempt: `esptool write-flash` (PASS), serial monitor (FAIL), standalone I²C scan firmware (zero devices), board then disconnected. Exact commands in `docs/MERGED_IMAGE.md` §6 |
| Blockers | **H2 open:** attached device had no responding I²C peripherals (expander/PMU/RTC/touch), so hardware did not pass; device disappeared before GPIO-level diagnosis. H1 (no board) is closed — a board was attached briefly |
| Stop/await condition | Complete the approved BSP repo; hardware acceptance still requires a powered, confirmed Rev 1 board |

### Interruption recovery (run these first)

```bash
source /home/d31337m3/devenv/env-6.1.sh          # Warroom IDF 6.1; never source export.sh directly
cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis
git status --short                               # uncommitted work is the live state
idf.py --version                                 # expect: ESP-IDF v6.1
head -20 docs/IMPLEMENTATION_PROGRESS.md          # this block
head -20 CHANGELOG.md                             # latest increment + verification result
```

---

## Work packages (AGENTS.md mapping)

| WP | Section | Deliverable | Status | Version introduced |
| --- | --- | --- | --- | --- |
| WP0 | §11 (tracking) | Progress ledger, changelog template, version bump | **DONE** | 0.3.0 |
| WP1 | §4 | Hardware bringup: IO expander → PMU → SH8601 AMOLED (≥30% brightness gate), touch, audio, RTC, 1-bit SDMMC, IMU; pin/delay/register doc | **DONE** (IDF 6.1 build passed) | 0.3.1 / 0.3.2 |
| WP2 | §5 | Provider abstraction: Kilo free default → OpenCode fallback → others; remove legacy paid-provider paths | **DONE** (IDF 6.1 build passed) | 0.4.0 / 0.4.1 |
| WP3 | §6 | LVGL on-screen keyboard + chat UI on same agent loop/message bus; legacy Telegram/Feishu inert | **DONE** (IDF 6.1 build passed) | 0.5.0 / 0.5.1 / 0.5.2 |
| WP4 | §7 | Storage split: flash (config/long-term/secrets) vs SD (sessions/history/cache/logs/temp); path schema + rotation limits | **DONE** (IDF 6.1 build passed) | 0.6.0 / 0.6.1 / 0.6.2 |
| WP5 | §8 | Power/RTC/low-power: charging, battery status, energy modes, wake sources, RTC alarms/timers → cron | **DONE** (final build passed) | 0.7.0 |
| WP6 | §9 | SD-card yolo filesystem tools (create/edit/move/copy/delete), SD-only boundary | **DONE** (final build passed) | 0.8.0 |
| WP7 | §10 | Embedded MicroPython component, on-demand only, SD-only write sandbox | **DONE** (IDF 6.1 build passed) | 0.9.0 |
| WP8 | §11 | Build attempt (IDF 6.1), push attempt, final DoD verification checklist | **DONE** | 1.0.0 |
| WP9 | operator follow-up | Merged 16 MB flash image; offline image verification; flash helper | **DONE** | 1.1.0 |
| WP10 | operator follow-up | Secondary board-spec BSP repo for board developers | **IN PROGRESS** — operator approved creation despite hardware FAIL (H2) | — |

**Order rationale:** WP1 establishes the display/touch bus and boot sequence every other package depends on. WP4 is done early because WP3/WP6/WP7 write through the storage policy. WP8 is last so it verifies the complete revision. WP9 produces the distributable image; WP10 is gated by the operator on physical hardware passing. WP0 exists because the operator requires documentation/versioning to lead implementation so interruptions cannot corrupt chronological progress.

---

## Increment log (append-only; newest first)

### 1.1.1 — 2026-11-18 — hardware flash attempt: write PASS, bringup FAIL

- **Summary:** A device appeared at `/dev/ttyACM0` and the merged image was written and hash-verified successfully. The flashed firmware booted (ESP32-S3, PSRAM, CsAW app) but hardware bringup failed: I²C expander `0x20` timed out, a standalone scan found zero I²C devices, and SD/UI initialization cascaded into errors. The device then disconnected. Fixed the esptool v5 option-order defect found during the attempt. Hardware is explicitly **NOT passed**.
- **Files touched:** `scripts/flash_genesis_merged.sh`, `docs/MERGED_IMAGE.md`, `VERSION.md`, `CHANGELOG.md`, this ledger.
- **Verification result:** flash `esptool ... write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/merged-binary.bin` → exit 0, `Hash of data verified`; boot log → `IO expander not found at 0x20: ESP_ERR_TIMEOUT`, `Hardware bringup FAILED`; I²C scan → zero devices `0x01..0x77` on SDA15/SCL14 at 400 kHz/100 kHz and swapped pins. Vendor source confirms `0x20`/SDA15/SCL14 are correct, so root cause is unresolved (likely unpowered/absent peripherals on the attached device).
- **Next:** WP10 — finish the operator-approved BSP repo with this failure clearly recorded.

### 1.1.0 — 2026-11-18 — WP9: merged Genesis flash image

- **Summary:** Produced the single-file 16 MB merged image for the Waveshare ESP32-S3-Touch-AMOLED-1.8 Rev 1 board and verified it byte-for-byte offline. Added a flash helper script and a release manifest. Physical hardware verification was attempted and is blocked (H1: no device attached), so the operator-gated secondary BSP repo (WP10) was not created; its content manifest is prepared below.
- **Files touched:** `docs/MERGED_IMAGE.md` (new), `scripts/flash_genesis_merged.sh` (new), `VERSION.md`, `CHANGELOG.md`, this ledger.
- **Verification result:** `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py merge-bin' > /tmp/opencode/csaw_merge_bin.log 2>&1` → exit 0, `Wrote 0xff0000 bytes to file 'merged-binary.bin'`, SHA-256 `bc99701b6d3d819de63e436391d5809129d5158af05313dd27dc6f4abc66d2ee`. Python region byte comparison → `MERGED_IMAGE_INTEGRITY = PASS`; all gaps `0xFF`. `esptool image-info`: bootloader checksum `0x86` valid, app checksum `0x08` valid, both Chip ID 9 / 16MB / DIO / 80m / ESP-IDF v6.1. `gen_esp32part.py`: table verifies OK. Hardware: `ls /dev/ttyUSB* /dev/ttyACM*` → none; `lsusb` → root hubs only; `esptool --chip esp32s3 chip-id` → `Could not connect to an Espressif device on any of the 8 available serial ports`.
- **Next:** WP10 — await operator decision, then create BSP repo.

### 1.0.0 — 2026-11-18 — WP8: final delivery and DoD verification

- **Summary:** Verified the complete Section 1–10 revision as one package. Clean IDF 6.1 build, GitHub push, final branding/storage/bringup checks and the DoD checklist below. README clone URLs point at the delivered repository.
- **Files touched:** `CHANGELOG.md`, `VERSION.md`, this ledger, `README.md`.
- **Verification result:** build command `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp7_build23.log 2>&1; echo "exit=$?"` → `exit=0`, `Project build complete. To flash, run:`, `csaw.bin binary size 0x1ecac0 bytes ... 0x13540 bytes (4%) free`. Branding: `grep -rni 'mimiclaw' --exclude-dir=.git --exclude-dir=build .` → 0 hits; `find . -path './.git' -prune -o -path './build' -prune -o -iname '*mimi*' -print` → 0 hits. Push: `gh repo create rrdlabs/csaw_esp32s3_genesis --private --confirm` → `https://github.com/rrdlabs/csaw_esp32s3_genesis` (exit 0); `git push -u origin master` → `* [new branch] master -> master`, tracking set, exit 0
- **Next:** none — revision complete.

### 0.9.0 — 2026-11-18 — WP7: on-demand MicroPython VM

- **Summary:** Vendored MicroPython v1.29.0 core, lib and prebuilt mpy-cross as an on-demand component driven only by `python_init` / `python_exec` / `python_eval`; no boot execution. Frozen `_boot.py` maps the HAL's IDF-VFS SD mount into Python via `vfs.VfsPosix`, and `csaw_sandbox.py` restricts writes to `/sd/csaw/python` while removing flash/NVS/remount APIs. Resolved the complete IDF 6.1 incompatibility set (enumerated in `docs/MICROPYTHON.md` §9), including the oofatfs/IDF-fatfs symbol collision and the lwip `memp_*` wrap symbols.
- **Files touched:** `components/csaw_micropython/**`, `main/python/csaw_python_vm.{c,h}` (new), `main/cli/serial_cli.c`, `main/CMakeLists.txt`, root `CMakeLists.txt`, `sdkconfig.defaults.esp32s3`, `docs/MICROPYTHON.md` (new), CHANGELOG/VERSION/ledger.
- **Verification result:** iterative builds `csaw_wp7_build5.log` … `csaw_wp7_build22.log` each recorded one concrete blocker and its fix; final `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp7_build23.log 2>&1; echo "exit=$?"` → `exit=0`, `Project build complete`. Frozen mpy-cross reports `MicroPython v1.29.0 on 2026-08-29; mpy-cross emitting mpy v6.3`.
- **Next:** WP8 — final build/push/DoD.

### 0.8.0 — 2026-11-18 — WP6: SD-card yolo filesystem tools

- **Summary:** Added `create_dir`, `move`, `copy` and `delete` agent tools with no confirmation prompt, all bounded by the SD workspace and audited to the SD log. `MAX_TOOLS` 16 → 24. Documented signatures, safety boundary and examples.
- **Files touched:** `main/tools/tool_files.{c,h}`, `main/tools/tool_registry.c`, `docs/SD_CARD_TOOLS.md` (new), CHANGELOG/VERSION/ledger.
- **Verification result:** `grep -n 'MAX_TOOLS' main/tools/tool_registry.c` → `24`; all four execute callbacks registered; path validation rejects non-`CSAW_SD_ROOT` paths. Built as part of the final clean build.
- **Next:** WP7 — embedded MicroPython.

### 0.7.0 — 2026-11-18 — WP5: power, RTC and low-power

- **Summary:** Implemented AXP2101 battery/backup-cell charging and status, PCF85063A alarms/timers, QMI8658 wake-on-shake, touch/wake-word wake sources, energy-save modes, status reporting to UI/CLI and RTC-alarm-driven cron scheduling. Documented the power state machine, thresholds and alarm semantics.
- **Files touched:** `components/csaw_hal/csaw_hal.c` + header, `main/power/csaw_power.{c,h}` (new), `main/cli/serial_cli.c`, `main/csaw.c`, `main/CMakeLists.txt`, `main/cron/cron_service.c`, `main/agent/agent_loop.c`, `docs/POWER_RTC.md` (new), `docs/HARDWARE_BRINGUP.md`, CHANGELOG/VERSION/ledger.
- **Verification result:** static symbol/doc review passed; the first WP5 build (`/tmp/opencode/csaw_wp5_build1.log`) exposed the IDF 6.1 `newlib` component error, resolved with the WP7 component work and confirmed by the final green build.
- **Next:** WP6 — SD filesystem tools.

### 0.6.2 — 2026-11-18 — WP4 verification ledger reconciliation

- **Summary:** Corrected the stale WP4 work-package status and 0.6.0 increment log to record the final successful IDF 6.1 build from `/tmp/opencode/csaw_wp4_build2.log`. No firmware behavior changed.
- **Files touched:** `docs/IMPLEMENTATION_PROGRESS.md`, CHANGELOG/VERSION.
- **Verification result:** Re-read `/tmp/opencode/csaw_wp4_build2.log` filter: `Project build complete. To flash, run:`; ledger and changelog now both reference the exact command and log path.
- **Next:** WP5 — power/RTC/low-power.

### 0.6.0 — 2026-11-18 — WP4: flash/SD storage split

- **Summary:** Added `csaw_storage` as the single enforcement point for the AGENTS.md §7 policy. Sessions, daily/working memory, logs and temp/tool files now live under `/sd/csaw/*` with rotation and file-count limits; flash keeps only long-term memory, config and provider secrets. Added SD audit logging and `storage_status` CLI diagnostics.
- **Files touched:** `main/storage/csaw_storage.{c,h}` (new), `main/csaw_config.h`, `main/memory/session_mgr.c`, `main/memory/memory_store.c`, `main/tools/tool_files.{c,h}`, `main/tools/tool_registry.c`, `main/agent/context_builder.c`, `main/agent/agent_loop.c`, `main/csaw.c`, `main/cli/serial_cli.c`, `main/CMakeLists.txt`, `docs/STORAGE_POLICY.md` (new), `config/storage.md`, `docs/ARCHITECTURE.md`, `README.md`, ledger/CHANGELOG/VERSION.
- **Verification result:** Static review: no remaining `CSAW_SPIFFS_SESSION_DIR`; file tools reject `/spiffs`; every high-churn writer routes through `csaw_storage_*`. Final build: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp4_build2.log 2>&1; grep -E 'error:|warning:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp4_build2.log | tail -40` → `Project build complete. To flash, run:` (after the 0.6.1 fix; no errors/warnings).
- **Next:** WP5 — power/RTC/low-power.

### 0.5.2 — 2026-11-18 — WP3 compile fix: ESP-IDF 6.1 panel ops header

- **Summary:** Added the missing `esp_lcd_panel_ops.h` include in `csaw_ui.c`; IDF 6.1 declares `esp_lcd_panel_draw_bitmap()` there, and the implicit declaration broke compilation of the LVGL flush callback.
- **Files touched:** `components/csaw_ui/csaw_ui.c`, CHANGELOG/VERSION, this ledger.
- **Verification result:** Rebuild command `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:`; no errors. Previous attempt (`build3.log`) failed with the implicit declaration.
- **Next:** WP4 — storage split (flash vs SD card).

### 0.5.1 — 2026-11-18 — WP3 build fix: vendored LVGL demo/example sources

- **Summary:** Fixed two CMake failures introduced by vendoring LVGL v9.6 without its upstream `examples/` and `demos/` trees: `esp.cmake` validates those `INCLUDE_DIRS`, and Kconfig defaults `LV_BUILD_EXAMPLES=y`/`LV_BUILD_DEMOS=y` add `demos/lv_demos.c` as a source. Added placeholder directories and disabled both options in the live `sdkconfig` and `sdkconfig.defaults.esp32s3` so clean builds stay reproducible.
- **Files touched:** `components/lvgl/examples/.gitkeep`, `components/lvgl/demos/.gitkeep` (new), `sdkconfig`, `sdkconfig.defaults.esp32s3`, CHANGELOG/VERSION, this ledger.
- **Verification result:** Build with this fix (`/tmp/opencode/csaw_wp3_build3.log`) passed CMake and reached compilation; the remaining compile error was the 0.5.2 header fix. Final combined verification `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:`. Prior attempt (`/tmp/opencode/csaw_wp3_build2.log`) failed with `Cannot find source file: .../components/lvgl/demos/lv_demos.c` and `No SOURCES given to target: __idf_lvgl`.
- **Next:** WP4 — storage split (flash vs SD card).

### 0.5.0 — 2026-11-18 — WP3: on-device keyboard/chat UI

- **Summary:** Vendored LVGL v9.6 as a local component; added a native SH8601 `esp_lcd_panel_t` flush path and FT3168 touch reads to the HAL; built the chat screen with on-screen keyboard; bridged keyboard submissions into the inbound message bus on channel `ui` and routed outbound `ui` replies back to the screen. Telegram/Feishu are now inert by default with an NVS-backed opt-in CLI and legacy UI labels.
- **Files touched:** `components/lvgl/**` (new), `components/csaw_hal/csaw_hal.c` + header, `components/csaw_ui/**` (new), `main/csaw.c`, `main/bus/message_bus.h`, `main/channels/legacy_channels.{c,h}` (new), `main/CMakeLists.txt`, `main/csaw_config.h`, `main/cli/serial_cli.c`, `main/onboard/onboard_html.h`, `docs/ON_DEVICE_UI.md` (new), `README.md`, ledger/CHANGELOG/VERSION.
- **Verification result:** Static review passed: brightness gate precedes panel creation and UI init; keyboard READY → `ui_send_bridge` → inbound bus → agent loop → outbound `ui` → `csaw_ui_show_response`; legacy channels skipped at init/start and outbound dispatch when disabled. Final build command `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:` (after the 0.5.1 and 0.5.2 fixes).
- **Next:** WP4 — storage split (flash vs SD card).

### 0.4.0 — 2026-11-18 — WP2: free-provider abstraction

- **Summary:** Replaced the dual paid-backend LLM proxy with a free-only ordered chain (Kilo → OpenCode → OpenRouter). Added runtime config for provider order, active entry, endpoints, models and tokens; all persisted in flash/NVS. Failover advances in memory only. Purged paid-provider strings from code, CLI, onboarding UI, docs and examples.
- **Files touched:** `main/llm/provider_chain.{c,h}` (new), `main/llm/llm_proxy.{c,h}`, `main/csaw_config.h`, `main/CMakeLists.txt`, `main/cli/serial_cli.c`, `main/onboard/onboard_html.h`, `main/csaw_secrets.h.example`, `main/agent/agent_loop.h`, `config/providers.md`, `docs/PROVIDER_CONFIG.md`, `docs/ARCHITECTURE.md`, `docs/TODO.md`, `docs/im-integration/FEISHU_SETUP.md`, `README*.md`, `skills/deploy/**`, `.github/workflows/release.yml`, ledger/CHANGELOG/VERSION.
- **Verification result:** Live catalog fetches confirmed Kilo `kilo-auto/free`, OpenCode `mimo-v2.6-flash-free` with chat base `https://opencode.ai/inference/openai/v1`, OpenRouter `inclusionai/ling-3.2-flash:free`. Residual-string scan returned only the official OpenCode URL path segment containing the legacy compatibility word; documented as a wire-format path, not a provider choice. Build command `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' 2>&1 | grep -E 'error:|FAILED|Built target|Project build complete|ninja: build stopped|IndexError|CMake Error' | tail -30` → `Project build complete` (v0.4.1).
- **Next:** WP3 — on-device LVGL keyboard/chat UI.

### 0.3.2 — 2026-11-18 — WP1 build verification + repo hygiene

- **Summary:** Completed WP1 verification: recorded the exact build command chain and its successful result, documented the three IDF 6.1/GCC 15 compiler fixes, and removed the inherited `components/` ignore rule so the new HAL and vendored cJSON are part of the delivered repo.
- **Files touched:** `main/wifi/wifi_manager.c`, `main/heartbeat/heartbeat.c`, `main/agent/agent_loop.c` (compiler fixes), `.gitignore`, ledger/CHANGELOG/VERSION.
- **Verification result:** Build attempt 3 command `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' 2>&1 | grep -E 'error:|FAILED|Built target|Project build complete|ninja: build stopped|IndexError|CMake Error' | tail -25` → `Project build complete. To flash, run:`; no errors. Real errors from build attempt 2 were read from `build/log/idf_py_stdout_output_589043` because IDF's hint generator crashed with `IndexError: Replacement index 2 out of range for positional args tuple`: (a) `WIFI_REASON_ASSOC_EXPIRE` undeclared, (b) `xTaskCreate` incompatible task signature, (c) GCC 15 `-Werror=hex-escape` in the agent status string. All fixed before attempt 3.
- **Next:** WP2 — provider abstraction.

### 0.3.1 — 2026-11-18 — WP1: hardware bringup HAL (build in progress)

- **Summary:** Added native ESP-IDF 6.1 HAL implementing the literal IO expander → PMU → SH8601 init chain, SH8601 QSPI register table, ≥30% brightness gate before UI, plus touch/audio/RTC/SDMMC/IMU stages. Recreated vendor QSPI macros with native APIs; vendored cJSON after IDF 6.1 removed the `json` component; switched project idf constraint to `>=6.1` and target to esp32s3.
- **Files touched:** `components/csaw_hal/{CMakeLists.txt,idf_component.yml,include/csaw_hal.h,csaw_hal.c}` (new), `components/cjson/{CMakeLists.txt,cJSON.c,cJSON.h}` (new, v1.7.19), `main/csaw.c` (call HAL before services), `main/CMakeLists.txt` (`csaw_hal`, `json`→`cjson`), `main/idf_component.yml` (`>=6.1`), `docs/HARDWARE_BRINGUP.md` (new), ledger/CHANGELOG/VERSION.
- **Verification result:** Source review complete: order, register table and brightness assertion match `docs/DISPLAY_BRINGUP.md`. Build attempt 1: `source /home/d31337m3/devenv/env-6.1.sh; idf.py build` → CMake error `Failed to resolve component 'json' required by component 'main': unknown name` (target also defaulted to esp32). Fixed by vendoring cJSON and setting target. Build attempt 2 (in progress at time of writing): `source /home/d31337m3/devenv/env-6.1.sh; idf.py set-target esp32s3 && idf.py build` — result to be appended here when available.
- **Next:** WP2 — provider abstraction.

### 0.3.0 — 2026-11-18 — WP0: implementation tracking infrastructure

- **Summary:** Established this ledger, an explicit changelog entry format, and version bump before any firmware code changes.
- **Files touched:** `docs/IMPLEMENTATION_PROGRESS.md` (new), `CHANGELOG.md` (template + entry), `VERSION.md`.
- **Verification result:** Ledger current-state block, work-package table and recovery commands present; changelog template mandates date/version/summary/files touched/verification result; VERSION.md = 0.3.0.
- **Next:** WP1 — hardware bringup foundation.

---

## Definition of Done — final verification (AGENTS.md §11)

| # | Requirement | Evidence | Result |
| --- | --- | --- | --- |
| 1 | Zero unapproved donor-brand strings | `grep -rni 'mimiclaw' --exclude-dir=.git --exclude-dir=build .` → 0 hits; filename scan → 0 hits; incidental English "mimic" in vendored upstream comments annotated in `docs/MICROPYTHON.md` | PASS |
| 2 | Clean ESP-IDF 6.1 build attempt recorded | `/tmp/opencode/csaw_wp7_build23.log`, exact command quoted above, exit `0`, `Project build complete`; merged rebuild produced `csaw.bin` 0x1ecad0 (4 % free) | PASS |
| 3 | GitHub push attempt recorded | `gh repo create rrdlabs/csaw_esp32s3_genesis --private --confirm` → `https://github.com/rrdlabs/csaw_esp32s3_genesis` (exit 0); `git push -u origin master` → `* [new branch] master -> master`, tracking set, exit 0 | PASS |
| 4 | Bringup order + ≥30% brightness verified in code/docs | `components/csaw_hal/csaw_hal.c:1094-1104` literal IO expander → PMU → SH8601; gate at `csaw_hal.c:489-496`; `main/csaw.c:157-165` asserts before UI; `docs/HARDWARE_BRINGUP.md` | PASS |
| 5 | Storage split verified in code/docs | `main/storage/csaw_storage.c`, `main/memory/session_mgr.c`, `main/tools/tool_files.c`, `docs/STORAGE_POLICY.md`; flash keeps only config/long-term memory/secrets | PASS |
| 6 | All docs and changelog entries present | `VERSION.md` = 1.0.0; `CHANGELOG.md` entries 0.7.0/0.8.0/0.9.0/1.0.0; `docs/` brings up, power/RTC, SD tools, MicroPython, UI, storage, providers, preflight, architecture | PASS |
| 7 | Free-only provider chain | `config/providers.md`, `docs/PROVIDER_CONFIG.md`, `main/llm/provider_chain.c`: Kilo (default) → OpenCode → OpenRouter | PASS |
| 8 | On-device keyboard/chat UI primary; legacy channels inert | `components/csaw_ui/**`, `main/channels/legacy_channels.c`, `docs/ON_DEVICE_UI.md` | PASS |
| 9 | RTC/alarm, wake, and SD yolo tools wired | `main/power/csaw_power.c`, `main/cron/cron_service.c`, `main/tools/tool_registry.c` (24 tools), `docs/POWER_RTC.md`, `docs/SD_CARD_TOOLS.md` | PASS |
| 10 | MicroPython on-demand, SD-only writes | `main/python/csaw_python_vm.c`, frozen `_boot.py` + `csaw_sandbox.py`, `docs/MICROPYTHON.md` | PASS |

---

## WP10 — secondary BSP repository (operator-approved, hardware FAIL recorded)

The operator specified: create a secondary BSP repo "if hardware passes", solely
for board developers needing this exact board spec support in their own
projects. A board was attached and flashed, but **bringup failed** (H2, see
`docs/MERGED_IMAGE.md` §6). When asked how to proceed, the operator chose
**"Create BSP now, mark pending"**. The repo `rrdlabs/csaw-genesis-bsp` is
therefore being created with the hardware failure explicitly recorded — not
silently presented as verified. Everything below is derived from the
build-verified firmware, not guessed.

| Proposed path | Source in `csaw_esp32s3_genesis` | Purpose |
| --- | --- | --- |
| `README.md` | `docs/HARDWARE_BRINGUP.md`, `docs/DISPLAY_BRINGUP.md` | Board spec, pin table, quick-start |
| `config/hardware.md` | `config/hardware.md` | Rev 1 pin map: I2C SDA15/SCL14, touch INT GPIO21, SDMMC CLK2/CMD1/D0=3, PMU 0x34, RTC 0x51, codec 0x18, IMU 0x6B/0x6A |
| `config/board.cmake` / `config/sdkconfig.board` | `sdkconfig.defaults.esp32s3`, `components/csaw_micropython/boards/CSAW_ESP32S3/*` | IDF 6.1 target, 16 MB flash/DIO/80m, octal PSRAM, partition table |
| `components/csaw_hal/**` | `components/csaw_hal/**` | Native ESP-IDF 6.1 HAL with literal expander → PMU → SH8601 bringup and ≥30 % brightness gate |
| `components/cjson/**` | `components/cjson/**` | cJSON v1.7.19 (IDF 6.1 removed `json`) |
| `components/sh8601/**`, `components/ft3168/**` (if split out) | `components/csaw_hal/csaw_hal.c` register table | Display/touch drivers extracted as reusable components |
| `config/partitions.csv` | `partitions.csv` | 2 MB ota_0/ota_1, 12 MB SPIFFS, coredump |
| `docs/HARDWARE_SOURCES.md` | `docs/HARDWARE_SOURCES.md` | Waveshare/Espressif source links for every hardware decision |
| `docs/PIN_MAP.md` | `docs/HARDWARE_BRINGUP.md` | Every pin, bus setting, reset delay and register write |
| `docs/VERIFIED_BUILD.md` | `docs/MERGED_IMAGE.md`, `/tmp/opencode/csaw_wp7_build23.log` | Exact IDF 6.1 toolchain, build/merge commands, artifact hash — **not** a claim of boot success |

The BSP repo must **not** contain the agent/provider/UI/MicroPython firmware:
its scope is board spec support only, so downstream projects bring their own
application. Name: `rrdlabs/csaw-genesis-bsp`. It is marked
**"hardware verification FAILED on first test (no I²C devices)"**, not merely
pending, until a powered, confirmed Rev 1 board produces a bringup log that
confirms the order and brightness gate.

**Operator decision applied:** the operator selected option (a) — create now
and mark the hardware status explicitly. No ambiguity was used to skip a
requirement: the flash was attempted, the failure was recorded verbatim, and
the BSP is published with that caveat.
