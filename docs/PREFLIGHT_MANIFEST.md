# CsAW Preflight Manifest

**Task:** AGENTS.md Section 1 — `agent-preflight`
**Date:** 2026-11-18
**Result:** Sources retrieved and staged locally. All three original blockers resolved by operator directive. No implementation performed.

## 1. Toolchain (RESOLVED — B1)

The ESP-IDF 6.1 environment is intentionally absent from the base `PATH`. It is activated through the operator's Warroom setup script, never by direct `export.sh` sourcing (which selects the wrong `IDF_TOOLS_PATH` and Python).

**Correct activation (verified):**

```bash
source /home/d31337m3/devenv/env-6.1.sh
# equivalent entry points: `warroom --devenv` -> "esp-idf-6.1";
#                          /home/d31337m3/devenv/launch-idf-6.1.sh
```

| Item | Value (verified output) |
| --- | --- |
| `IDF_PATH` | `/home/d31337m3/devenv/esp-idf-6.1` |
| `IDF_TOOLS_PATH` | `/home/d31337m3/devenv/.idf-tools/6.1` |
| `IDF_PYTHON_ENV_PATH` | `/home/d31337m3/devenv/.idf-tools/6.1/python_env/idf6.1_py3.12_env` |
| `python3` | Python 3.12.16 (`~/.local/uvpy312` selected by `env-6.1.sh`) |
| `idf.py --version` | **ESP-IDF v6.1** |
| `cmake` | 4.4.4 (base shell) |
| Legacy env | `/home/d31337m3/devenv/env.sh` / `esp-idf-5.2.2` — NOT to be layered with 6.1 |

The previously recorded failure (`source /home/d31337m3/devenv/esp-idf-6.1/export.sh` → missing `idf6.1_py3.14_env`, then `No module named 'rich_click'`) was caused by bypassing the Warroom wrapper. Resolved by using the wrapper; no toolchain reinstall performed.

## 2. Retrieved sources

| Item | Version / commit | URL | SHA-256 | Local path |
| --- | --- | --- | --- | --- |
| Waveshare board repo (Rev 1 original) | `01fd3200370a4417b257e8a4c185a04ef931342a` | https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8.git | git commit | `third_party/waveshare/esp32-s3-touch-amoled-1.8-rev1/` |
| Waveshare wiki page (saved HTML/text) | retrieved 2026-11-18 | https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8 | see file | `docs/sources/waveshare-wiki.html`, `.txt` |
| BSP v1.1.4 (SH8601 / FT5x06 — Rev 1) | 1.1.4, commit `c77caf968fa6b11f3b6a416c853c578d012a8cea` | https://components-file.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/1.1.4/waveshare__esp32_s3_touch_amoled_1_8-v1.1.4.zip | `80ee01275034b1b882a2d580e7ca5c0b85f172abdc5328f2935dc19e359cbfa9` | `third_party/espressif/waveshare_bsp_v1.1.4/` |
| BSP v2.0.3 (CO5300 / CST820 — V2, reference only) | 2.0.3, commit `9f4030c6e5cb888ad4cc268bfa7584c93ad53e30` | https://components-file.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/2.0.3/waveshare__esp32_s3_touch_amoled_1_8-v2.0.3.zip | `50b28157042af95e4d625eaa386a470dd3dc87869be8a2ae2ce9c43c8d8d8698` | `third_party/espressif/waveshare_bsp_v2.0.3/` |
| LVGL | v9.6.0 | https://github.com/lvgl/lvgl/releases/tag/v9.6.0 | `b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df` | `third_party/lvgl/` |
| MicroPython | v1.29.0 | https://github.com/micropython/micropython/releases/tag/v1.29.0 | `8072f6ad42c0e3bbfca88ed09bf8f1106ce207a39129af920f70081b8f2fe286` | `third_party/micropython/` |

Registry metadata from `https://components.espressif.com/api/components/waveshare/esp32_s3_touch_amoled_1_8` and `.../esp_lcd_sh8601`.

## 3. Component versions pinned by vendor manifests

Rev 1 BSP v1.1.4 `idf_component.yml`: `waveshare/esp_lcd_sh8601: *`, `esp_lcd_touch_ft5x06: *`, `esp_io_expander_tca9554: ^2`, `esp_lcd_panel_io_additions: ^1`, `esp_codec_dev: ~1.5`, `espressif/esp_lvgl_port: ^2`, `lvgl/lvgl: >=8,<10`, **`idf: '>=5.3'`**.
Vendor example `00_bsp_quickstart/main/idf_component.yml`: `waveshare/esp32_s3_touch_amoled_1_8: ^2.0.3`, `idf: ">=5.5,<6.1"`.

## 4. Resolved blockers / operator directives

**B1 — Toolchain (RESOLVED).** Use Warroom's `env-6.1.sh` (verified above); never source `export.sh` directly or alter base `PATH`. This is intentional environment isolation by the operator.

**B2 — IDF version constraint (RESOLVED by operator directive).**
> "ignore vendor manifest and follow the BSP allow of >5.3 by using 6.1 and modifying/reworking/recreating any component that doesnt want to play nice. Operational hardware is the target with this blocker - not specificity of idf versioning via manifests/vendoring. We will republish this BSP as the definitive complete idf 6.1+ compatable driver and device support package for this specific board upon completion."

Implementation will: use IDF 6.1; supply its own project/component manifests (not the vendor example's `<6.1` constraint); adapt or recreate any component that does not compile against 6.1; record every adaptation in `docs/HARDWARE_SOURCES.md` for the planned BSP republication. No downgrade to IDF 5.x.

**B3 — Init sequence (RESOLVED by operator directive).**
> "using any of the example applications/demos via the waveshare wiki for this board (That use the display) follow line by line - the exact - bringup sequence to enable qspi display with proper endianess, rgb order, and min 30% brightness on init, while ensuring that init is possible by ensuring ioexpander is enabled via power rails (pmu) and that the follow the demo/example sequence exactly."

The line-by-line extraction from `01_HelloWorld`, `04_GFX_FT3168_Image`, `00_bsp_quickstart`, `14_lvgl_demo_v9`, and the Rev 1 BSP is recorded in **`docs/DISPLAY_BRINGUP.md`**. It includes QSPI pin/bus config, RGB element order, brightness command, register/init-command table with delays, and the IO-expander power-rail enable + PMU ordering. One conflict remains noted (not a blocker): the vendor BSP defines but does not call `bsp_io_expander_init()` in `bsp_display_start()`; the CsAW init path will call the expander and PMU stages explicitly in the mandated order before the SH8601 stage, as required by AGENTS.md §4.1 and the operator directive.

## 5. Verification performed

- Official board repo identified via GitHub API (org `waveshareteam`), not a third-party fork.
- SH8601/FT3168 confirmed as Rev 1; CO5300/CST820 is V2 (reference only).
- All archives hashed; registry metadata and vendor manifests recorded.
- IDF 6.1 activation verified end-to-end via Warroom wrapper.
- Exact demo init sequence, QSPI settings, RGB order, and brightness command extracted with source line references.

## 6. Completion gate

Every required source category has an authoritative source, version, license reference, and local path. All blockers resolved/documented. **STOP** — await authorization before implementation tasks (AGENTS.md §§3–11).
