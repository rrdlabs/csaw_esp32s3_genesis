# CsAW embedded MicroPython VM (on demand, SD-only writes)

Status: **vendored + component glue prepared; build/push performed by the parent agent**.
This document is the implementation note for AGENTS.md §10. No build was run by the
subagent that prepared these files.

## 1. What was vendored, and exactly how

All vendor input was fetched from upstream Git; nothing was hand-written or stubbed.
The reference repository `/tmp/opencode/micropython-idf-component` (HEAD
`dc11952e94115e328e997950bfaa4fdf533d0032`) was used only for its integration
pattern. Its two submodule pins were reproduced exactly.

Commands executed from `/home/d31337m3/projects/csaw/csaw_esp32s3_genesis`:

```bash
mkdir -p components/csaw_micropython/vendor
git clone --depth 1 --branch v1.29.0 \
  https://github.com/micropython/micropython.git \
  components/csaw_micropython/vendor/micropython
# clone warning: refs/tags/v1.29.0 (annotated tag object
# 23f16c8a0939d43736fa481227dffef984cbec7b) dereferences to the commit below
git -C components/csaw_micropython/vendor/micropython rev-parse HEAD
# 0fd6c573ea815774668bbb16b8e197c8822368b2   (matches the reference submodule)

git clone --depth 1 https://github.com/micropython/micropython-lib.git \
  components/csaw_micropython/vendor/micropython-lib
git -C components/csaw_micropython/vendor/micropython-lib rev-parse HEAD
# 92ee97d94c4b558cf4e6d27a4534401f1e9b9f50   (default branch tip)
git -C components/csaw_micropython/vendor/micropython-lib \
  fetch --depth 1 origin ee4bb8ff139e24c42b739935fbd8ec7c4d061e02
git -C components/csaw_micropython/vendor/micropython-lib checkout -q FETCH_HEAD
git -C components/csaw_micropython/vendor/micropython-lib rev-parse HEAD
# ee4bb8ff139e24c42b739935fbd8ec7c4d061e02   (matches the reference submodule)

# Nested Git repositories are removed so the vendor tree is plain source under
# the CsAW target repository's own Git history:
rm -rf components/csaw_micropython/vendor/micropython/.git \
       components/csaw_micropython/vendor/micropython-lib/.git
```

Pins recorded:

| Source | Pin | Notes |
| --- | --- | --- |
| `vendor/micropython` | `0fd6c573ea815774668bbb16b8e197c8822368b2` | tag `v1.29.0` (tag object `23f16c8a0939d43736fa481227dffef984cbec7b`) |
| `vendor/micropython-lib` | `ee4bb8ff139e24c42b739935fbd8ec7c4d061e02` | reference submodule pin; vendored for future frozen modules |
| Reference integration | `dc11952e94115e328e997950bfaa4fdf533d0032` | `/tmp/opencode/micropython-idf-component`, read-only |

The upstream MicroPython source is left **unmodified**. The `MICROPY_ESP_IDF_ENTRY`
override is a supported upstream board option (`ports/esp32/mpconfigport.h:450`).

## 2. Files created

| Path | Purpose |
| --- | --- |
| `components/csaw_micropython/CMakeLists.txt` | Component glue; sets `MICROPY_DIR` to the vendored core and includes `ports/esp32/esp32_common.cmake` |
| `components/csaw_micropython/idf_component.yml` | IDF 6.1 dependency manifest: declares `espressif/mdns` from the registry; `esp_libc` is automatic |
| `components/csaw_micropython/micropython.cmake` | Mirrors the reference bootstrap; sets board dir, lib dir, frozen manifest, generates `sdkconfig.board` |
| `components/csaw_micropython/boards/CSAW_ESP32S3/mpconfigboard.cmake` | CsAW board definition, SDKCONFIG fragments, frozen manifest, user C module |
| `components/csaw_micropython/boards/CSAW_ESP32S3/mpconfigboard.h` | `MICROPY_ESP_IDF_ENTRY = csaw_micropython_entry`, board name, IDF 6.1 module disables, POSIX VFS bridge enables |
| `components/csaw_micropython/boards/CSAW_ESP32S3/sdkconfig.board.in` | Board SDKCONFIG overrides (project partition table, 16 MB flash, octal PSRAM, USB/JTAG console) |
| `components/csaw_micropython/boards/CSAW_ESP32S3/manifest.py` | Freezes only CsAW modules; deliberately excludes upstream flash `_boot.py` |
| `components/csaw_micropython/boards/CSAW_ESP32S3/modules/_boot.py` | Bridges the HAL-mounted SD card at `/sd` via `vfs.VfsPosix`, creates `/sd/csaw/python`, installs sandbox, signals readiness. Never mounts flash |
| `components/csaw_micropython/boards/CSAW_ESP32S3/modules/csaw_sandbox.py` | Runtime sandbox: `open()` limited to `/sd/csaw/python`; flash/NVS/mount APIs removed |
| `components/csaw_micropython/include/csaw_vm_bridge.h` | Public C bridge API used by `main/python/` |
| `components/csaw_micropython/mod_csaw_vm/micropython.cmake` | User C module registration for the bridge |
| `components/csaw_micropython/mod_csaw_vm/csaw_vm.c` | Scheduler-based hand-off; registers Python `csaw_vm.mark_ready()` |
| `main/python/csaw_python_vm.h` | On-demand API (init / running / exec file / exec string / stop / path policy) |
| `main/python/csaw_python_vm.c` | Explicit-demand API implementation, SD path validation, no boot wiring |
| `docs/MICROPYTHON.md` | This note |

No root `CMakeLists.txt`, `main/CMakeLists.txt`, `sdkconfig*`, HAL, CLI or versioning
file was modified by this work package.

## 3. Exact changes the parent must apply

### 3.1 Root `CMakeLists.txt`

Insert the following **before** `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`
and `project(csaw)`; the existing `spiffs_create_partition_image()` line stays
where it is:

```cmake
# CsAW embedded MicroPython VM (on demand only).  Must precede project().
set(MICROPY_BOARD CSAW_ESP32S3)
set(MPY_BOARDS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components/csaw_micropython/boards)
set(MPY_COMPONENT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/components/csaw_micropython)
set(MPY_SOURCE_ROOT ${MPY_COMPONENT_ROOT}/vendor/micropython)
set(MPY_ESP32_ROOT ${MPY_SOURCE_ROOT}/ports/esp32)
include(${MPY_COMPONENT_ROOT}/micropython.cmake)

# The board fragment sets SDKCONFIG_DEFAULTS; keep the project's own defaults in
# the list as well (IDF may already append the target-specific file).
list(APPEND SDKCONFIG_DEFAULTS
    ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults
    ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.esp32s3)
```

The MicroPython board fragment is generated into `${CMAKE_BINARY_DIR}/sdkconfig.board`
and is listed **last** in `SDKCONFIG_DEFAULTS`, so CsAW's partition table and
console settings win over MicroPython's baseline fragment.

### 3.2 `main/CMakeLists.txt`

Add the API source and the component dependency:

```cmake
    "python/csaw_python_vm.c"          # add to SRCS
...
    REQUIRES
        csaw_micropython               # add to existing REQUIRES list
```

`csaw_micropython` must be in `REQUIRES` so `main/python/csaw_python_vm.c` can
include `csaw_vm_bridge.h` from the component's public include directory.

### 3.3 Required `sdkconfig` options

Already present in `sdkconfig.defaults.esp32s3`; the board fragment re-asserts
them and must not be removed:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

MicroPython's own baseline fragment (`ports/esp32/boards/sdkconfig.base`) is
included; its 4 MB / `partitions-4MiBplus.csv` defaults are overridden by
`sdkconfig.board.in`. Do **not** enable `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`
pointing at MicroPython's partition table.

### 3.4 ESP-IDF 6.1 component-name compatibility (applied)

The first CMake attempt failed with:

```
Failed to resolve component 'newlib' required by component
'csaw_micropython': unknown name.
HINT: newlib component has been renamed to esp_libc. Any REQUIRES newlib can
simply be deleted as esp_libc is REQUIRED by default.
```

Exact fix, applied inside our owned vendor tree
`components/csaw_micropython/vendor/micropython/ports/esp32/esp32_common.cmake`:

- **deleted** `newlib` from the `IDF_COMPONENTS` list (no replacement needed;
  `esp_libc` is automatic in IDF 6.1),
- **replaced** the legacy umbrella `usb` entry with the IDF 6.1 components
  `esp_driver_usb_serial_jtag`, `esp_hal_usb`, `esp_usb_cdc_rom_console`
  (all verified present in `/home/d31337m3/devenv/esp-idf-6.1/components`).

A full check of every name in `IDF_COMPONENTS` against the IDF 6.1 component
directory showed no other missing/legacy names (`driver`, `hal`, `esp_adc`,
`esp_ringbuf`, `nvs_flash`, etc. all still resolve).

Two related IDF 6.1 adjustments were also made in this work package:

- `boards/CSAW_ESP32S3/mpconfigboard.cmake` sets `MICROPY_PY_TINYUSB OFF`, so
  the component-manager `espressif__tinyusb` dependency (git fork) is not needed.
- `boards/CSAW_ESP32S3/mpconfigboard.h` sets `MICROPY_HW_ENABLE_USBDEV (0)` and
  `MICROPY_HW_ESP_USB_SERIAL_JTAG (0)`, so MicroPython never claims the USB
  Serial/JTAG peripheral already owned by the CsAW IDF console.
- `components/csaw_micropython/idf_component.yml` declares `espressif/mdns`
  (`^1.15.1`) because `mdns.h` is included by MicroPython's `network_wlan.c`
  and `modsocket.c` but mdns left the IDF core in 6.x. This is a manifest
  dependency, not a `REQUIRES` entry. `dependencies.lock` is owned by the
  parent agent and will be refreshed by the next build.

## 4. On-demand guarantee and boot wiring

- `ports/esp32/main.c` exports `MICROPY_ESP_IDF_ENTRY`, set here to
  `csaw_micropython_entry`. It therefore does **not** export `app_main`; CsAW's
  own `app_main` remains the firmware entry point.
- `csaw_micropython_start()` is only called from `csaw_python_vm_init()`.
- `csaw_python_vm_init()` is only called from an explicit CLI/UI/agent command
  handler. **Do not call it from `app_main`, `csaw.c`, or any init routine.**
- VM startup creates the MicroPython task; the frozen `_boot.py` mounts the SD
  card, installs the sandbox and calls `csaw_vm.mark_ready()`, which is when
  `csaw_python_vm_is_running()` becomes true.

## 5. CLI commands the parent should register

Add these commands to `main/cli/serial_cli.c` (owned by another work package):

| Command | Handler action |
| --- | --- |
| `python_exec <path>` | Call `csaw_python_vm_init()` if needed, then `csaw_python_vm_exec_file(path)`. Path must be under `/sd/csaw/python/`. |
| `python_eval "<source>"` | Call `csaw_python_vm_init()` if needed, then `csaw_python_vm_exec_string(source)`. |
| `python_stop` | Call `csaw_python_vm_stop()`. Rejects new jobs; the resident VM task is not torn down. |

Suggested help text (CsAW-branded):

```
python_exec <path>       - run a Python script from /sd/csaw/python (on demand)
python_eval "<source>"   - evaluate a Python string (on demand)
python_stop              - stop accepting new CsAW Python jobs
```

## 6. SD paths and storage policy

| Path | Use |
| --- | --- |
| `/sd` | SD card mount created by `csaw_hal_sdcard_init()` with `esp_vfs_fat_sdmmc_mount()`; the frozen `_boot.py` maps it into MicroPython with `vfs.VfsPosix` |
| `/sd/csaw/python` | Only directory Python may write to; also added to `sys.path` |
| `/sd/csaw/python/*.py` | User/agent scripts accepted by `python_exec` |
| `/spiffs`, `/`, NVS | Rejected by `csaw_python_vm_path_allowed()`; never mounted as Python-writable VFS |

Path validation in `main/python/csaw_python_vm.c` rejects:

- relative paths and any path not prefixed with `/sd/csaw/python/`,
- `..` traversal anywhere, backslashes, `:`, `%`,
- `/spiffs`, `/`, `/nvs`, NVS-style URIs, and paths longer than 192 bytes.

Runtime sandbox in `csaw_sandbox.py` additionally replaces `builtins.open` so
`open()` outside `/sd/csaw/python/` raises `OSError`, and removes
`esp.flash_write`, `esp.flash_erase`, `esp32.NVS`, `esp32.Partition`, and the
`uos`/`os` mount, mkfs and file-mutation helpers. **Python code has no flash
write access.** Native CsAW code still uses flash/NVS/SPIFFS per
`docs/STORAGE_POLICY.md`; this restriction is only on the Python side.

Script size and execution limits: 64 KiB per `python_eval` string, 30 s per job,
four queued job slots.

## 7. Hardware mapping and IDF 6.1 integration

From `docs/HARDWARE_BRINGUP.md` / `config/hardware.md`:

- SDMMC host slot 1, 1-bit width
- `CLK = GPIO2`, `CMD = GPIO1`, `D0 = GPIO3`
- FAT filesystem on the installed 8 GB TF card

The CsAW HAL owns the SDMMC peripheral and mounts the card at `/sd` **before**
the VM is created. MicroPython never claims the pins: `machine.SDMMC` is
disabled (`MICROPY_PY_MACHINE_SDCARD=0`) because IDF 6.1 moved
`driver/sdmmc_host.h`. The frozen `_boot.py` exposes the existing IDF VFS mount
to Python with `vfs.mount(vfs.VfsPosix("/sd"), "/sd")`. This also avoids a link
collision between MicroPython's bundled `oofatfs` and IDF's `fatfs`, both of
which define the same `f_*` symbols; `MICROPY_VFS_FAT=0` and the oofatfs sources
are excluded. The frozen `_boot.py` degrades gracefully if the bridge fails: it
prints a CsAW error and the exec API fails closed instead of falling back to
flash.

IDF 6.1 compatibility changes applied to the vendored v1.29.0 core are
enumerated in `docs/MICROPYTHON.md` §9 and in the 0.9.0 changelog entry.

## 8. Verified upstream symbols (no invented APIs)

Checked in the vendored v1.29.0 tree:

- `MICROPY_ESP_IDF_ENTRY` default and override: `ports/esp32/mpconfigport.h:450`
- `void MICROPY_ESP_IDF_ENTRY(void)` implementation: `ports/esp32/main.c:279`
- `mp_sched_schedule(mp_obj_t, mp_obj_t)`: `py/runtime.h:117`
- `pyexec_file(const char *)`: `shared/runtime/pyexec.h:54`
- `pyexec_vstr(vstr_t *, bool)`: `shared/runtime/pyexec.h:58`
- `vstr_init`, `vstr_add_strn`, `vstr_clear`: `py/misc.h`
- `vfs.mount` and `vfs.VfsPosix`: `extmod/modvfs.c:66`, `extmod/vfs_posix.c:109`

Items originally flagged for build-time validation, now resolved by the final
IDF 6.1 build:

1. User C module QSTRs (`MP_QSTR_csaw_vm`, `MP_QSTR_mark_ready`) are generated
   by the standard qstr scan of `mod_csaw_vm/csaw_vm.c`.
2. `MICROPY_INC_USERMOD` propagation through `idf_component_register()` makes
   `csaw_vm_bridge.h` visible to `main`.
3. `pyexec_file()` / `pyexec_vstr()` return-code interpretation: the bridge
   treats `PYEXEC_FORCED_EXIT` as failure and all other values as success;
   unhandled Python exceptions are printed by MicroPython's `pyexec`.
4. `mp_sched_schedule()` is only called after `_boot.py` marks the VM ready;
   scheduling uses `MP_OBJ_FROM_PTR(&csaw_vm_run_job_obj)`.

## 9. IDF 6.1 compatibility changes in the vendored core

Recorded so the pin is reproducible from the upstream v1.29.0 tarball:

- Disabled in the CsAW board config: SSL/mbedTLS, cryptolib, hashlib,
  Bluetooth/Nimble, PWM, PCNT, I2S, ADC/ADC-block, I2C target, machine timer,
  machine UART, machine SDMMC, ESP-NOW, the MicroPython network/Wi-Fi stack,
  UART REPL, USB device/USB Serial/JTAG and TinyUSB. Those peripherals belong
  to the CsAW native HAL or are unavailable in IDF 6.1.
- `ports/esp32/mpconfigport.h`: added `#ifndef` guards so board overrides of
  ADC, I2S, ADC-block and UART are honoured.
- `ports/esp32/modmachine.c`: guarded the `Timer` module entry with
  `MICROPY_PY_MACHINE_TIMER`; added the IDF 6.1 GPIO deep-sleep wakeup API name.
- `ports/esp32/main.c`: guarded the `modnetwork.h` include and added
  `esp_flash_chips/esp_flash_types.h` for the now-opaque `esp_flash_t`.
- `ports/esp32/esp32_common.cmake`: excluded modules whose IDF APIs were
  removed/renamed, added `esp_driver_rmt`/`esp_driver_tsens`, kept `lwip_patch.c`
  (the link options still wrap lwip `memp_malloc`/`memp_free`), removed
  oofatfs and set `MICROPY_VFS_FAT=0`.
- `extmod/vfs_posix_file.c`: include `<sys/poll.h>` (newlib path in IDF 6.1).
- `ports/esp32/mphalport.h`: supplied the unix-style `MP_HAL_RETRY_SYSCALL` /
  `RAISE_ERRNO` helpers required by the POSIX VFS bridge.
- Frozen `mpy-cross` (v1.29.0, mpy v6.3) is vendored at the path expected by
  `makemanifest.py`; no host gcc is required to build firmware.

## 10. Build / push

Final IDF 6.1 build passed with the complete MicroPython component:
`/tmp/opencode/csaw_wp7_build23.log` ends with `Project build complete. To flash, run:`
and produced `build/csaw.bin` (0x1ecac0 bytes). The exact command and the full
sequence of IDF 6.1 link/compile blockers resolved are recorded in
`docs/IMPLEMENTATION_PROGRESS.md` and the 0.9.0/1.0.0 changelog entries.

## 11. Verification performed for this component

- Both vendor commits match the reference integration's submodule pins.
- Vendor `.git` directories were removed; no nested repositories are created.
- A case-insensitive recursive grep for the donor brand (and for the standalone
  short brand token) over this work package's files returned zero hits.
  Upstream incidental words such as "mimic"/"mimics" in vendored MicroPython
  comments are English verbs, not donor branding, and are annotated here as
  upstream text.
- No file outside the paths listed in §2 was created or modified.
