# CsAW Changelog

All notable changes to this repository are documented here. Every change bumps the version in [VERSION.md](VERSION.md) and appends an entry below.

**Required entry format (AGENTS.md §11 — no exceptions, so interruptions cannot lose chronological state):**

```
## [VERSION] - YYYY-MM-DD

### Summary
<what changed and why>

### Files touched
- <path> — <change>

### Verification result
- <command run> -> <observed result, or exact blocker/attempted command>
```

Chronological implementation state is tracked in [docs/IMPLEMENTATION_PROGRESS.md](docs/IMPLEMENTATION_PROGRESS.md).

## [1.1.0] - 2026-11-18

### Summary

Built and verified the single-file merged flash image for the Waveshare ESP32-S3-Touch-AMOLED-1.8 Rev 1 ("Genesis") board. The raw 16 MB image (`build/merged-binary.bin`, SHA-256 `bc99701b…`) embeds bootloader, partition table, otadata, app and SPIFFS at their flash offsets. Every region was byte-compared against its source binary, padding was confirmed `0xFF`, both image headers verified valid ESP32-S3/IDF 6.1 checksums and hashes, and the partition table parsed cleanly. Added a flash helper script and a release manifest. Physical hardware verification was attempted but is **blocked**: no board/serial device is attached in this environment, so the operator-gated secondary BSP repository was not created.

### Files touched

- `docs/MERGED_IMAGE.md` (new) — release manifest, artifact digests, partition layout, reproduce/flash commands, offline verification table, exact hardware blocker commands, physical-board checklist.
- `scripts/flash_genesis_merged.sh` (new) — port-parameterised esptool wrapper for the merged image.
- `VERSION.md`, `CHANGELOG.md`, `docs/IMPLEMENTATION_PROGRESS.md` — 1.1.0 increment and hardware gate decision.

### Verification result

- Merge: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py merge-bin' > /tmp/opencode/csaw_merge_bin.log 2>&1` → exit `0`, `Successfully created ESP32-S3 image.`, `Wrote 0xff0000 bytes to file 'merged-binary.bin', ready to flash to offset 0x0.`
- Integrity: Python byte comparison of all five regions inside `merged-binary.bin` → `MERGED_IMAGE_INTEGRITY = PASS`; all inter-region gaps `0xFF`.
- Headers: `esptool image-info bootloader/bootloader.bin` → checksum `0x86` valid; `esptool image-info csaw.bin` → checksum `0x08` valid, Chip ID 9, 16MB/DIO/80m, ESP-IDF v6.1.
- Partitions: `gen_esp32part.py partition-table.bin` → `Verifying table...` OK.
- Script: `bash -n scripts/flash_genesis_merged.sh` → syntax OK.
- Hardware: `ls /dev/ttyUSB* /dev/ttyACM*` → none; `lsusb` → root hubs only; `esptool --chip esp32s3 chip-id` → `A fatal error occurred: Could not connect to an Espressif device on any of the 8 available serial ports.` Exact blocker recorded in `docs/MERGED_IMAGE.md` §6.

## [1.0.0] - 2026-11-18

### Summary

WP8 / final delivery of the complete Section 1–10 revision as one package. The full firmware builds cleanly with the Warroom-provisioned ESP-IDF 6.1 toolchain, the repository is pushed to GitHub, and the Definition of Done checklist is verified: zero unapproved donor-brand strings, literal expander → PMU → SH8601 bringup with the ≥30% brightness assertion, enforced flash/SD storage split, and all docs present. README clone URLs now point at the delivered repository.

### Files touched

- `CHANGELOG.md`, `VERSION.md`, `docs/IMPLEMENTATION_PROGRESS.md` — final increment, DoD checklist, build/push evidence.
- `README.md` — clone URL updated to the delivered GitHub repository.
- No further firmware behavior changed in this increment.

### Verification result

- Clean build: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp7_build23.log 2>&1` → exit `0`, `Project build complete. To flash, run:`, `Generated .../build/csaw.bin`, size `0x1ecac0` bytes (`4%` free in the 0x200000 app partition).
- Branding: `grep -rni 'mimiclaw' --exclude-dir=.git --exclude-dir=build .` → zero hits; `find . -iname '*mimi*'` → zero hits. Only incidental English "mimic"/"mimics" remains in vendored upstream comments, annotated as upstream text in `docs/MICROPYTHON.md`.
- Bringup order verified in `components/csaw_hal/csaw_hal.c:1094` (`csaw_hal_io_expander_init()` → `csaw_hal_pmu_init()` → `csaw_hal_display_init()`) with the brightness gate at `csaw_hal.c:489`.
- Storage split verified in `main/storage/csaw_storage.c`, `main/memory/session_mgr.c`, `main/tools/tool_files.c` and `docs/STORAGE_POLICY.md`.
- Push: `gh repo create rrdlabs/csaw_esp32s3_genesis --private --confirm` → `https://github.com/rrdlabs/csaw_esp32s3_genesis` (exit 0); `git remote add origin https://github.com/rrdlabs/csaw_esp32s3_genesis.git` → exit 0; `git push -u origin master` → `To https://github.com/rrdlabs/csaw_esp32s3_genesis.git / * [new branch] master -> master / branch 'master' set up to track 'origin/master'.` (exit 0)

## [0.9.0] - 2026-11-18

### Summary

WP7: vendored MicroPython v1.29.0 (source pin `0fd6c573ea815774668bbb16b8e197c8822368b2`, lib `ee4bb8ff139e24c42b739935fbd8ec7c4d061e02`, reference integration `dc11952e94115e328e997950bfaa4fdf533d0032`) as an on-demand component. The VM never auto-starts: `csaw_python_vm_init()` is called only by `python_init`, `python_exec` or `python_eval` on the serial CLI / agent. A frozen `_boot.py` maps the HAL's IDF-VFS SD mount into MicroPython with `vfs.VfsPosix` and `csaw_sandbox.py` restricts every write to `/sd/csaw/python`, stripping flash/NVS and VFS-remount APIs. A large IDF 6.1 compatibility patch set made the vendored core link cleanly (details in `docs/MICROPYTHON.md` §9).

### Files touched

- `components/csaw_micropython/**` (new) — vendored core/lib/mpy-cross, CsAW board config, frozen modules, `mod_csaw_vm` C bridge, IDF 6.1 patches to `mpconfigport.h`, `modmachine.c`, `main.c`, `esp32_common.cmake`, `mphalport.h`, `vfs_posix_file.c`.
- `main/python/csaw_python_vm.{c,h}` (new), `main/cli/serial_cli.c` (`python_init`, `python_exec`, `python_eval`, `python_stop`), `main/CMakeLists.txt`.
- `CMakeLists.txt`, `sdkconfig.defaults.esp32s3` — bootstrap, mpy-cross path, ALPN and CRT-bundle restores.
- `docs/MICROPYTHON.md` (new), CHANGELOG/VERSION, ledger.

### Verification result

- Builds 5–23 in `/tmp/opencode/csaw_wp7_build*.log` record every compile/link blocker and fix: newlib/usb/tinyusb, mbedtls/Nimble/PCNT/LEDC, `soc/uart_periph.h`, mpy-cross path, `SOC_I2S_NUM`, ADC/I2C target, network/Wi-Fi, RMT/tsens, `esp_flash_t`, bridge/RMT, legacy-channel CRT bundle, 55 legacy `strncpy` sites demoted to warnings, RTC include, lwip `memp_*` wrappers, ALPN, Timer/SDCard module guards, oofatfs/IDF-fatfs collision, `sys/poll.h`, POSIX syscall helpers.
- Final verification: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp7_build23.log 2>&1; echo "exit=$?"` → `exit=0`, `Project build complete`.
- Sandbox static review: `builtins.open` guarded to `/sd/csaw/python/`; `esp.flash_write`, `esp.flash_erase`, `esp32.NVS`, `esp32.Partition`, mount/mkfs/mutation helpers and `vfs.mount`/`VfsPosix` removed after boot.

## [0.8.0] - 2026-11-18

### Summary

WP6: extended the agent tool registry with unrestricted (yolo) SD-card filesystem operations — `create_dir`, `move`, `copy` and `delete` alongside the existing `read_file`/`write_file`/`list_dir`. All paths are confined to `CSAW_SD_ROOT` (`/sd/csaw`); `/spiffs`, flash and NVS paths are rejected and every mutation is written to the SD audit log. No confirmation prompt is required. `MAX_TOOLS` raised from 16 to 24.

### Files touched

- `main/tools/tool_files.{c,h}` — recursive create, move (rename with copy+delete fallback), bounded byte-wise copy/tree copy and guarded recursive delete.
- `main/tools/tool_registry.c` — registrations and `MAX_TOOLS 24`.
- `docs/SD_CARD_TOOLS.md` (new) — signatures, safety boundary and examples.
- CHANGELOG/VERSION, ledger.

### Verification result

- Static review: `grep -n 'MAX_TOOLS' main/tools/tool_registry.c` → `24`; `grep -nE 'tool_create_dir_execute|tool_move_execute|tool_copy_execute|tool_delete_execute' main/tools/tool_registry.c` → all four registered; each validates paths against `CSAW_SD_ROOT` before acting.
- Built as part of the final 0.9.0/1.0.0 clean build; no WP6-specific build errors remained.

## [0.7.0] - 2026-11-18

### Summary

WP5: completed power, RTC and low-power behavior. AXP2101 battery-level/charging and RTC backup-cell charging management, PCF85063A programmable alarms/timers, QMI8658 wake-on-shake, touch and wake-word wake sources, energy-save modes with brightness scaling, and a `csaw_power` service that reports status to the UI/CLI, logs to SD and arms the next cron job from an RTC alarm. Power state machine, charging thresholds and alarm semantics documented.

### Files touched

- `components/csaw_hal/csaw_hal.c`, `components/csaw_hal/include/csaw_hal.h` — PMU battery/backup charging/status, RTC alarm/timer control, IMU wake-on-shake.
- `main/power/csaw_power.{c,h}` (new) — power state machine, thresholds, wake logging.
- `main/cli/serial_cli.c` — `power_status`, `power_mode`, `rtc_time`, `rtc_alarm` commands.
- `main/csaw.c`, `main/CMakeLists.txt`, `main/cron/cron_service.c`, `main/agent/agent_loop.c` — service init, alarm→cron and wake-word→scheduler wiring.
- `docs/POWER_RTC.md` (new), `docs/HARDWARE_BRINGUP.md`, CHANGELOG/VERSION, ledger.

### Verification result

- Static review: `grep -nE 'csaw_power_|battery|backup_charg|csaw_hal_rtc_set_alarm|wake_on_shake|wake_word' main/power/ main/cli/ components/csaw_hal/csaw_hal.c` → all symbols present; `docs/POWER_RTC.md` documents thresholds and alarm semantics.
- WP5-era build (`/tmp/opencode/csaw_wp5_build1.log`) initially failed with the `newlib` component resolution error; resolved by the WP7 IDF 6.1 component work and confirmed by the final clean build in `/tmp/opencode/csaw_wp7_build23.log`.

## [0.6.2] - 2026-11-18

### Summary

Documentation-only reconciliation: the WP4 ledger row and the 0.6.0 increment still described the build as pending even after the successful 0.6.1 rebuild. Both now record the exact command and `/tmp/opencode/csaw_wp4_build2.log` result. No firmware behavior changed.

### Files touched

- `docs/IMPLEMENTATION_PROGRESS.md` — WP4 status and 0.6.0 verification result corrected; 0.6.2 increment appended.
- CHANGELOG/VERSION — version bump and this entry.

### Verification result

- `grep -E 'error:|warning:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp4_build2.log | tail -40` → `Project build complete. To flash, run:`; ledger/changelog now agree with that result.

## [0.6.1] - 2026-11-18

### Summary

WP4 compile fix: GCC 15 treats `-Wformat-truncation` as an error in this project. `csaw_storage_prune()` copied a 255-byte FAT filename into a 160-byte buffer with `snprintf("%s")`; replaced it with an explicit length check and `strcpy` so oversized names are skipped safely.

### Files touched

- `main/storage/csaw_storage.c` — bounded filename copy in `csaw_storage_prune()`.
- CHANGELOG/VERSION, `docs/IMPLEMENTATION_PROGRESS.md` — version/tracking update.

### Verification result

- Prior build (`/tmp/opencode/csaw_wp4_build.log`): `csaw_storage.c:165:69: error: '%s' directive output may be truncated writing up to 255 bytes into a region of size 160 [-Werror=format-truncation=]`.
- Rebuild verification: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp4_build2.log 2>&1; grep -E 'error:|warning:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp4_build2.log | tail -40` → `Project build complete. To flash, run:`; no errors or warnings.

## [0.6.0] - 2026-11-18

### Summary

WP4: enforced the flash/SD storage split (AGENTS.md §7). Added a central `csaw_storage` policy module that prepares the `/sd/csaw/*` path schema, rotates and bounds high-churn files, prunes old files and writes SD logs. Session history, daily notes, heartbeat working memory and agent file tools now live on the SD card; only long-term memory, config and secrets remain in flash/NVS/SPIFFS. Added `storage_status` CLI diagnostics and documented the schema and limits.

### Files touched

- `main/storage/csaw_storage.{c,h}` (new) — SD path schema, `mkdir -p`, rotated/bounded writes, pruning, SD logging, status.
- `main/csaw_config.h` — SD path schema constants and size/rotation limits; heartbeat moved to SD.
- `main/memory/session_mgr.c` — sessions at `/sd/csaw/sessions/<safe_chat_id>.jsonl` with rotation and sanitized IDs.
- `main/memory/memory_store.c` — daily notes at `/sd/csaw/context/daily/`, long-term `MEMORY.md` still in flash but capped.
- `main/tools/tool_files.c`, `tool_files.h`, `main/tools/tool_registry.c` — SD-only file tools; `/spiffs` explicitly rejected; tool writes audited in SD log.
- `main/agent/context_builder.c`, `main/agent/agent_loop.c` — prompt/tool guidance names SD paths and the primary UI instead of legacy IM.
- `main/csaw.c` — call `csaw_storage_init()` before high-churn consumers.
- `main/cli/serial_cli.c` — `storage_status` command.
- `main/CMakeLists.txt` — new storage source.
- `docs/STORAGE_POLICY.md` (new), `config/storage.md`, `docs/ARCHITECTURE.md`, `README.md`, CHANGELOG/VERSION, ledger.

### Verification result

- Static review: `grep` confirms no `CSAW_SPIFFS_SESSION_DIR` remains, file-tool validation rejects `/spiffs`, and all high-churn writers call `csaw_storage_*`.
- IDF 6.1 build verification (final, after the 0.6.1 fix): `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp4_build2.log 2>&1; grep -E 'error:|warning:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp4_build2.log | tail -40` → `Project build complete. To flash, run:` (no errors/warnings).

## [0.5.2] - 2026-11-18

### Summary

WP3 compile fix: the LVGL flush callback calls `esp_lcd_panel_draw_bitmap()`, which ESP-IDF 6.1 declares in `esp_lcd_panel_ops.h` (not in the already-included panel interface/IO headers). Added the explicit include so the UI component builds under the Warroom IDF 6.1 toolchain.

### Files touched

- `components/csaw_ui/csaw_ui.c` — include `esp_lcd_panel_ops.h`.
- CHANGELOG/VERSION, `docs/IMPLEMENTATION_PROGRESS.md` — version/tracking update.

### Verification result

- Prior build (`/tmp/opencode/csaw_wp3_build3.log`): `components/csaw_ui/csaw_ui.c:58:9: error: implicit declaration of function 'esp_lcd_panel_draw_bitmap' [-Wimplicit-function-declaration]`.
- Rebuild verification: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:`; no errors.

## [0.5.1] - 2026-11-18

### Summary

Build fix for the WP3 LVGL integration: the vendored `components/lvgl` copy contains the core sources but not the upstream `examples/` and `demos/` trees. Upstream Kconfig defaults (`LV_BUILD_EXAMPLES=y`, `LV_BUILD_DEMOS=y`, inherited by the generated `sdkconfig`) made `env_support/cmake/esp.cmake` reference the missing `demos/lv_demos.c`, aborting CMake before compilation. Empty placeholder directories were added for the include-dir validation, and the example/demo build options are now explicitly disabled in `sdkconfig` and `sdkconfig.defaults.esp32s3` so fresh configuration rebuilds keep working.

### Files touched

- `components/lvgl/examples/.gitkeep`, `components/lvgl/demos/.gitkeep` — placeholder directories required by `esp.cmake` `INCLUDE_DIRS`.
- `sdkconfig`, `sdkconfig.defaults.esp32s3` — `CONFIG_LV_BUILD_EXAMPLES=n`, `CONFIG_LV_BUILD_DEMOS=n`.

### Verification result

- First rebuild after placeholder dirs: `CMake Error ... Cannot find source file: .../components/lvgl/demos/lv_demos.c` + `No SOURCES given to target: __idf_lvgl` (exact log `/tmp/opencode/csaw_wp3_build2.log`).
- Fix applied; subsequent rebuild (`/tmp/opencode/csaw_wp3_build3.log`) passed CMake and reached C compilation, exposing an unrelated missing header fixed in 0.5.2. Final verification after both fixes: `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:`.

## [0.5.0] - 2026-11-18

### Summary

WP3: primary on-device UX — LVGL v9.6 keyboard/chat screen on the SH8601 AMOLED with FT3168 touch, wired to the existing message bus and agent loop. Added a native SH8601 panel handle for LVGL flush, FT3168 register-level touch reads, automatic reply routing on the new `"ui"` channel, and made legacy Telegram/Feishu channels inert by default with an NVS-backed opt-in CLI.

### Files touched

- `components/lvgl/` (new) — vendored LVGL v9.6.0 component and enabled `lv_conf.h`.
- `components/csaw_hal/csaw_hal.c`, `include/csaw_hal.h` — native `esp_lcd_panel_t` (0x2A/0x2B/0x2C flush, mirror/swap/gap/brightness), FT3168 `csaw_hal_touch_read()` using vendor registers 0x02–0x06; panel creation after the brightness gate.
- `components/csaw_ui/` (new) — LVGL display/PSRAM buffers/1 ms tick/touch indev/group, chat screen, on-screen keyboard, history and API (`csaw_ui_init`, `csaw_ui_set_send_cb`, `csaw_ui_show_response`, `csaw_ui_show_status`).
- `main/csaw.c` — early UI init after hardware bringup/brightness gate, `ui_send_bridge` to inbound bus, outbound `"ui"` rendering, legacy channel guards.
- `main/bus/message_bus.h` — `CSAW_CHAN_UI "ui"`.
- `main/channels/legacy_channels.{c,h}` (new), `main/CMakeLists.txt` — opt-in defaults off and NVS-persisted runtime control.
- `main/csaw_config.h` — UI chat id and legacy-channel defaults/namespace.
- `main/cli/serial_cli.c` — `legacy_channel_set <telegram|feishu> <0|1>`.
- `main/onboard/onboard_html.h` — primary-UX notice and legacy labels.
- `docs/ON_DEVICE_UI.md` (new), `README.md`, `docs/IMPLEMENTATION_PROGRESS.md`, `VERSION.md` — documentation and tracking.

### Verification result

- Static review: brightness gate is asserted in `csaw_hal_display_init()` before `csaw_hal_display_panel_create()` and before `csaw_ui_init()`; keyboard submit path reaches `message_bus_push_inbound()` and outbound `"ui"` reaches `csaw_ui_show_response()`.
- IDF 6.1 build verification (final, after the 0.5.1/0.5.2 build fixes): `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' > /tmp/opencode/csaw_wp3_build4.log 2>&1; grep -E 'error:|FAILED|Project build complete|ninja: build stopped|CMake Error' /tmp/opencode/csaw_wp3_build4.log | tail -30` → `Project build complete. To flash, run:` (no errors).

## [0.4.1] - 2026-11-18

### Summary

Recorded the successful ESP-IDF 6.1 build verification of the WP2 provider refactor.

### Files touched

- `docs/IMPLEMENTATION_PROGRESS.md` — WP2 verification result appended.
- `CHANGELOG.md`, `VERSION.md` — chronological tracking.

### Verification result

- `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' 2>&1 | grep -E 'error:|FAILED|Built target|Project build complete|ninja: build stopped|IndexError|CMake Error' | tail -30` → `Project build complete. To flash, run:`; no errors.

## [0.4.0] - 2026-11-18

### Summary

WP2: replaced the legacy two-backend LLM code with a free-only provider abstraction and ordered fallback chain — Kilo free router (default) → OpenCode → OpenRouter. Removed all paid-provider request builders, headers, default IDs, CLI choices and onboarding UI options. Provider selection, tokens and endpoints are runtime-configurable and stored in flash/NVS; automatic failover stays in memory to avoid flash wear.

### Files touched

- `main/llm/provider_chain.{c,h}` (new) — three free provider descriptors, NVS-backed runtime config, ordered chain parsing, fallback on 401/403/404/408/409/429/5xx, in-memory failover.
- `main/llm/llm_proxy.{c,h}` — single chat-completions wire format, per-provider model/key/endpoint, chain loop with automatic advance, legacy dual-protocol parsing removed.
- `main/csaw_config.h` — free-router endpoints/models/defaults; paid endpoints and model IDs removed.
- `main/CMakeLists.txt` — compile `provider_chain.c`.
- `main/cli/serial_cli.c` — `provider_status`, `set_provider_chain`, `set_provider_model`, `set_provider_url`, `set_provider_key`; provider help strings now name only free routers.
- `main/onboard/onboard_html.h` — onboarding provider select lists Kilo/OpenCode/OpenRouter; API key marked optional.
- `main/csaw_secrets.h.example`, `skills/deploy/SKILL.md`, `skills/deploy/scripts/validate.sh`, `.github/workflows/release.yml` — free-tier setup instructions.
- `config/providers.md`, `docs/PROVIDER_CONFIG.md`, `docs/ARCHITECTURE.md`, `docs/TODO.md`, `docs/im-integration/FEISHU_SETUP.md`, `README.md`, `README_CN.md`, `README_JA.md`, `main/agent/agent_loop.h` — documentation/comment updates.
- `docs/IMPLEMENTATION_PROGRESS.md`, `VERSION.md` — chronological tracking.

### Verification result

- Source scan: `grep -rniE 'anthropic|openai|claude|gpt-[0-9]|sk-ant-' --exclude-dir=third_party --exclude-dir=build --exclude-dir=.git .` → only `main/csaw_config.h:94` remains, which is the literal official OpenCode chat path `https://opencode.ai/inference/openai/v1` (router wire-compatibility segment, documented in `docs/PROVIDER_CONFIG.md`). No provider selection or UI string names a legacy paid provider.
- Live catalog evidence (2026-11-18): `GET https://opencode.ai/inference/v1/models` → free IDs including `mimo-v2.6-flash-free`, chat `base_url=https://opencode.ai/inference/openai/v1`; `GET https://ai-gateway.kilo.ai/api/v1/models` → `kilo-auto/free`; `GET https://openrouter.ai/api/v1/models` → `inclusionai/ling-3.2-flash:free`.
- IDF 6.1 build verification: in progress (exact command recorded in `docs/IMPLEMENTATION_PROGRESS.md`); result appended on completion.

## [0.3.2] - 2026-11-18

### Summary

Recorded the completed WP1 IDF 6.1 build verification and the three compiler fixes it exposed, and corrected `.gitignore` so the vendored product components are not silently excluded from the repository.

### Files touched

- `main/wifi/wifi_manager.c` — removed `WIFI_REASON_ASSOC_EXPIRE` case (removed in IDF 6.1).
- `main/heartbeat/heartbeat.c` — `heartbeat_send_task(void *arg)` signature required by `xTaskCreate`.
- `main/agent/agent_loop.c` — split adjacent hex escape and ASCII literal to fix GCC 15 `-Werror=hex-escape`.
- `.gitignore` — removed blanket `components/` ignore; added explicit product-component tracking and `third_party/` reference-source ignore.
- `docs/IMPLEMENTATION_PROGRESS.md`, `CHANGELOG.md`, `VERSION.md` — chronological tracking.

### Verification result

- `cd /home/d31337m3/projects/csaw/csaw_esp32s3_genesis && bash -c 'source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1; idf.py build' 2>&1 | grep -E 'error:|FAILED|Built target|Project build complete|ninja: build stopped|IndexError|CMake Error' | tail -25` → `Project build complete. To flash, run:` (exit 0, no errors).
- `git check-ignore -v components/csaw_hal/csaw_hal.c` → not ignored (verified below).

## [0.3.1] - 2026-11-18

### Summary

WP1: native ESP-IDF 6.1 hardware bringup HAL for board Rev 1 — literal IO expander → PMU → SH8601 init chain, QSPI/RGB config, ≥30% brightness gate before UI, and post-display peripheral stages. IDF 6.1 incompatibilities recreated locally per operator directive.

### Files touched

- `components/csaw_hal/` (new) — `csaw_hal.c`, `include/csaw_hal.h`, `CMakeLists.txt`, `idf_component.yml`: I2C0, TCA9554 rail enable (LOW→20 ms→HIGH + readback), AXP2101 ID/rail check, SH8601 software reset + 9-command init table + `0x51` brightness gate, FT3168/ES8311/PCF85063A/SDMMC/QMI8658 stages.
- `components/cjson/` (new) — vendored cJSON v1.7.19 (MIT) to replace the `json` component removed in IDF 6.1.
- `main/csaw.c` — `csaw_hal_init()` called before all services; logs brightness-gate assertion.
- `main/CMakeLists.txt` — added `csaw_hal`; `json` → `cjson`.
- `main/idf_component.yml` — idf constraint `>=6.1`.
- `docs/HARDWARE_BRINGUP.md` (new) — pin map, bus settings, reset delays, register writes, IDF 6.1 adaptations.
- `docs/IMPLEMENTATION_PROGRESS.md`, `VERSION.md` — chronological tracking.

### Verification result

- `source /home/d31337m3/devenv/env-6.1.sh; idf.py build` → failed at configure: `Failed to resolve component 'json' required by component 'main': unknown name`; default target was esp32. Both corrected by vendoring cJSON and `idf.py set-target esp32s3`.
- `source /home/d31337m3/devenv/env-6.1.sh; idf.py set-target esp32s3 && idf.py build` → in progress (backgrounded); result to be appended on completion.
- Source-level check: init order and register table match `docs/DISPLAY_BRINGUP.md`; brightness gate asserts ≥30% before UI.

## [0.3.0] - 2026-11-18

### Summary

WP0: established interruption-proof implementation tracking before firmware work — chronological progress ledger, explicit changelog entry format, and version bump.

### Files touched

- `docs/IMPLEMENTATION_PROGRESS.md` — new append-only ledger: current state, interruption recovery commands, WP1–WP8 map to AGENTS.md §§4–11, increment log.
- `CHANGELOG.md` — required entry format template + this entry.
- `VERSION.md` — bumped to 0.3.0.

### Verification result

- Ledger present with current state = WP0 / v0.3.0, next action = WP1, recovery commands (`env-6.1.sh`, `git status`, `idf.py --version`) recorded.
- Changelog template requires date, version, summary, files touched, verification result.

## [0.2.1] - 2026-11-18

### Added

- `docs/DISPLAY_BRINGUP.md`: line-by-line extraction of the exact Rev 1 display bringup from Waveshare demos (`01_HelloWorld`, `04_GFX_FT3168_Image`, `00_bsp_quickstart`, `14_lvgl_demo_v9`) and BSP v1.1.4 — QSPI pin/bus config, RGB element order, SH8601 register/init table with delays, brightness command (`0x51`, >=30% floor), and IO-expander power-rail + PMU ordering.

### Changed

- `docs/PREFLIGHT_MANIFEST.md`: blockers B1–B3 resolved with operator directives; verified Warroom IDF 6.1 activation recorded (`ESP-IDF v6.1`, Python 3.12, `IDF_TOOLS_PATH=.../.idf-tools/6.1`).
- `docs/HARDWARE_SOURCES.md`, `config/hardware.md`: updated with verified pin map and resolved blocker status.
- Version bumped to 0.2.1.

### Verification

- `source /home/d31337m3/devenv/env-6.1.sh` → `idf.py --version` = `ESP-IDF v6.1` (previously failing direct `export.sh` bypass recorded as the cause).
- Demo evidence cross-checked against staged vendor sources; no downstream implementation started.

## [0.2.0] - 2026-11-18

### Added

- Workspace `pre-major-task-run.md` runbook, registered in `AGENTS.md` as Task 1 (`agent-preflight`); AGENTS.md sections renumbered sequentially 0–11 with cross-references updated.
- Staged third-party sources under `third_party/`: official Waveshare board repo (commit `01fd3200...`), Waveshare BSP v1.1.4 (Rev 1 / SH8601) and v2.0.3 (V2 reference), LVGL v9.6.0, MicroPython v1.29.0, saved Waveshare wiki page.
- Preflight records: `docs/PREFLIGHT_MANIFEST.md`, `docs/HARDWARE_SOURCES.md`, `docs/PROVIDER_CONFIG.md`, `config/hardware.md`, `config/providers.md`, `config/storage.md`.

### Changed

- Version bumped to 0.2.0 per AGENTS.md Section 11.

### Verification

- All retrieved archives hashed; component registry metadata and vendor manifests recorded with versions and commit SHAs.
- SH8601/FT3168 confirmed as Rev 1; 30% brightness floor supported by vendor `bsp_display_brightness_set()`.
- Provider endpoints recorded from official Kilo/OpenCode documentation; Kilo `kilo-auto/free` default with anonymous access confirmed.

### Blockers recorded (no implementation attempted)

- B1: ESP-IDF 6.1 Python venv `idf6.1_py3.14_env` missing; `idf.py` unavailable in this shell.
- B2: vendor example manifest declares `idf: >=5.5,<6.1` (BSP component itself allows `>=5.3`).
- B3: vendor sources do not fully evidence the mandated IO Expander → PMU → AMOLED sequence; operator confirmation requested before hardware work.

## [0.1.0] - 2026-11-18

### Added

- Complete project tree relocated into the CsAW repository from the donor source tree. The donor itself was not modified.
- `VERSION.md`, `CHANGELOG.md`, and `docs/RENAME_REPORT.md`.
- CsAW-branded vector assets: `assets/csaw_banner.svg` and `assets/csaw_diagram.svg`.

### Changed

- All product branding, identifiers, macro prefixes, filenames, CLI prompts, log tags, onboarding AP name, hostname, firmware artifact names, build project name, docs, and SPIFFS payload references renamed to CsAW / `csaw` / `CSAW`.
  - 840 lines changed across 59 text files.
  - Renamed: `main/csaw.c`, `main/csaw_config.h`, `main/csaw_secrets.h.example`.
  - Renamed: `assets/csaw_diagram.svg` (formerly the branded raster diagram).
- Upstream repository links in badges/docs rewritten to the placeholder `csaw/csaw_esp32s3_genesis` path. Update to the real remote URL when the GitHub remote is configured.
- Branded raster banner/diagram removed from the delivered tree and replaced by the CsAW vector assets above.

### Verification

- Zero residual donor-brand strings in file contents (case-insensitive, excluding `.git`).
- Zero residual donor-brand strings in filenames.
- Zero stale references to the removed raster assets.
- Build references consistent: `project(csaw)`, `main/CMakeLists.txt` compiles `csaw.c`, config includes `csaw_secrets.h`, log tag `csaw`, gitignore covers `csaw_secrets.h`.
- Build and GitHub push attempts are intentionally deferred to the consolidated hardware/provider revision described in the workspace agent instructions.

### Notes

- The image-generation service was unavailable (invalid credential), so the two branded raster assets were replaced with locally authored CsAW SVG assets rather than edited in place.
- The custom ESP-IDF 6.1 toolchain provisioned via Warroom was confirmed by the operator as available locally for the subsequent hardware-bringup revision.
