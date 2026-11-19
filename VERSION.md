# CsAW Version

Current revision: **1.1.0**

| Version | Date | Summary |
| --- | --- | --- |
| 1.1.0 | 2026-11-18 | Release artifact: merged 16 MB flash image for the Genesis Rev 1 board, byte-level image verification, flash helper, and recorded hardware-verification blocker. |
| 1.0.0 | 2026-11-18 | WP8: complete single-revision delivery — clean IDF 6.1 build, GitHub push, final DoD checklist and verification. |
| 0.9.0 | 2026-11-18 | WP7: vendored on-demand MicroPython VM, SD-only write sandbox, IDF 6.1 compatibility patch set; clean build. |
| 0.8.0 | 2026-11-18 | WP6: unrestricted SD-card file tools (create_dir/move/copy/delete) confined to `/sd/csaw`. |
| 0.7.0 | 2026-11-18 | WP5: battery/backup charging, status, energy modes, touch/shake/wake-word wake and RTC alarms wired to cron. |
| 0.6.2 | 2026-11-18 | Documentation-only: reconciled WP4 build verification status in ledger and changelog. |
| 0.6.1 | 2026-11-18 | WP4 compile fix: avoid GCC 15 format-truncation error in SD prune. |
| 0.6.0 | 2026-11-18 | WP4: flash/SD storage split with `/sd/csaw/*` schema, rotation/pruning limits, SD logs and `storage_status` CLI. |
| 0.5.2 | 2026-11-18 | WP3 compile fix: include `esp_lcd_panel_ops.h` for LVGL flush callback on IDF 6.1. |
| 0.5.1 | 2026-11-18 | Build fix: disable LVGL example/demo source builds for the vendored component and satisfy required include dirs. |
| 0.5.0 | 2026-11-18 | WP3: LVGL on-screen keyboard/chat UI on SH8601/FT3168, wired to the message bus; legacy Telegram/Feishu inert by default. |
| 0.4.1 | 2026-11-18 | Recorded successful IDF 6.1 build verification of the WP2 free-provider chain. |
| 0.4.0 | 2026-11-18 | WP2: free-only provider abstraction and Kilo → OpenCode → OpenRouter fallback chain; legacy paid-provider paths and strings removed. |
| 0.3.2 | 2026-11-18 | Recorded successful IDF 6.1 WP1 build, three compiler fixes, and corrected `.gitignore` so product components are tracked. |
| 0.3.1 | 2026-11-18 | WP1: native ESP-IDF 6.1 hardware bringup HAL (IO expander → PMU → SH8601, ≥30% brightness gate), vendored cJSON, IDF 6.1 compile fixes. |
| 0.3.0 | 2026-11-18 | WP0: interruption-proof implementation tracking (progress ledger, changelog format, version bump) before firmware changes. |
| 0.2.1 | 2026-11-18 | Resolved all three preflight blockers via Warroom toolchain verification and operator directives: ESP-IDF 6.1 accepted (vendor manifest non-binding), and the exact Waveshare demo display bringup (QSPI/RGB/brightness/expander+PMU) extracted to `docs/DISPLAY_BRINGUP.md`. |
| 0.2.0 | 2026-11-18 | Added `agent-preflight` as AGENTS.md Task 1 (sections renumbered 0–11) and executed it: retrieved/pinned vendor and third-party sources, recorded hardware/provider/config inputs and blockers. |
| 0.1.0 | 2026-11-18 | Complete donor-tree rename and relocation into the CsAW repository. Branding, identifiers, filenames, CLI/log/UI strings, docs, and graphic assets are now CsAW. |

See [CHANGELOG.md](CHANGELOG.md) for full details and [docs/IMPLEMENTATION_PROGRESS.md](docs/IMPLEMENTATION_PROGRESS.md) for chronological implementation state.
