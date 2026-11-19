# CsAW Rename & Relocation Report

**Revision:** 0.1.0
**Date:** 2026-11-18
**Scope:** Rename and relocation only. Hardware, provider, UI, storage, power, tooling, and MicroPython work is explicitly out of scope for this revision.

## 1. Source and destination

- Source (read-only donor): sibling donor tree at the workspace root, left untouched
- Destination: repository root (`csaw_esp32s3_genesis`)
- The destination's existing `.git` directory was preserved; no donor `.git` data was copied.
- 96 project files were copied; the donor working tree remains clean and unmodified.

## 2. Transformations applied

| Category | Result |
| --- | --- |
| Product name | `CsAW` (title case) |
| Lowercase identifiers, hostnames, artifact names | `csaw` |
| Uppercase macros / prefixes | `CSAW_` |
| Filenames | `main/csaw.c`, `main/csaw_config.h`, `main/csaw_secrets.h.example`, `assets/csaw_diagram.svg` |
| CLI prompt / log tag | `csaw>` / `csaw` |
| Build project | `project(csaw)` |
| Onboarding AP prefix | `CsAW-XXXX` |
| Docs, badges, external links | CsAW naming; repo links point at the delivered `rrdlabs/csaw_esp32s3_genesis` repository |

Totals: **840 lines changed across 59 files**.

## 3. Graphic assets

The donor supplied two legacy-branded raster images (a banner and an architecture diagram). Automated image editing was attempted but failed because the image service returned an invalid-credential error.

Deterministic fallback applied:

- Added `assets/csaw_banner.svg` — CsAW-branded banner.
- Added `assets/csaw_diagram.svg` — CsAW architecture overview (on-screen chat/keyboard, voice route, serial CLI, agent loop, tools, SD card memory, free model providers).
- Updated all Markdown references to use the SVG assets.
- Removed the two branded rasters from the delivered tree.
- Retained `assets/esp32s3-usb-port.jpg` (hardware photograph, no branding).

## 4. Verification performed

- Case-insensitive content search across all files except `.git` for the legacy brand token and its uppercase variant: **no matches**.
- Filename search (`-iname`) for the legacy brand token: **no matches**.
- Search for stale references to either removed raster filename: **no matches**.

Build-reference sanity checks:

- `CMakeLists.txt` → `project(csaw)`
- `main/CMakeLists.txt` → compiles `csaw.c`
- `main/csaw_config.h` → includes `csaw_secrets.h`, defines `CSAW_*`
- `main/csaw.c` → `static const char *TAG = "csaw";`
- `.gitignore` → ignores `csaw_secrets.h`

## 5. Deferred / operator follow-ups

- External badge and download URLs use `rrdlabs/csaw_esp32s3_genesis`, the repository pushed in WP8.
- Firmware build and GitHub push attempts are part of the single consolidated revision specified in the workspace agent instructions and were not attempted in this rename-only pass.
- ESP-IDF 6.1 via Warroom was confirmed available locally by the operator; no toolchain changes were made in this revision.
- Provider backends, Telegram defaults, flash-vs-SD storage policy, power/RTC behavior, SD filesystem yolo tools, and MicroPython remain as inherited from the donor and are subject to the subsequent mandated revisions.
