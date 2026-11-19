# CsAW board definition for the embedded MicroPython VM.
set(SDKCONFIG_DEFAULTS
    # MicroPython's baseline ESP-IDF settings only.  Do NOT include its BLE,
    # 240 MHz or SPIRAM fragments: CsAW's own sdkconfig defaults already set
    # those values for the ESP32-S3-Touch-AMOLED-1.8 board.
    ${MPY_ESP32_ROOT}/boards/sdkconfig.base

    # Keep this last: CsAW overrides for partition table, flash size, PSRAM and
    # console.  This file is generated from sdkconfig.board.in by
    # components/csaw_micropython/micropython.cmake.
    ${CMAKE_BINARY_DIR}/sdkconfig.board
)

set(IDF_TARGET esp32s3)
set(MICROPY_PY_BTREE OFF)
# CsAW diagnostics use the USB Serial/JTAG console, not MicroPython's USB CDC.
# Disabling TinyUSB avoids the component-manager espressif__tinyusb dependency
# (not vendored) and the IDF 6.1 usb-component rename.
set(MICROPY_PY_TINYUSB OFF)

# The CsAW sandbox needs no frozen micropython-lib modules; the vendored
# micropython-lib tree is still present for future use.  Freeze only CsAW's own
# modules so the upstream flash-mounting _boot.py is never compiled in.
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# CsAW's user C module provides the C<->MicroPython hand-off (on-demand exec,
# readiness flag).  It is linked into the MicroPython interpreter target.
set(USER_C_MODULES ${MPY_COMPONENT_ROOT}/mod_csaw_vm)
list(APPEND MICROPY_INC_USERMOD ${MPY_COMPONENT_ROOT}/include)
