# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# CsAW embedded MicroPython VM (on demand only). This bootstrap must precede
# project(); the VM owns no app_main and is started only by explicit CLI/agent
# demand. See docs/MICROPYTHON.md for pins, sandbox and trigger commands.
set(MICROPY_BOARD CSAW_ESP32S3)
set(MPY_BOARDS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components/csaw_micropython/boards)
set(MPY_COMPONENT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/components/csaw_micropython)
set(MPY_SOURCE_ROOT ${MPY_COMPONENT_ROOT}/vendor/micropython)
set(MPY_ESP32_ROOT ${MPY_SOURCE_ROOT}/ports/esp32)

# Frozen CsAW sandbox modules need mpy-cross. The build host has no C compiler,
# so use the pinned prebuilt MicroPython v1.29.0 mpy-cross vendored under tools/
# (installed from PyPI mpy-cross==1.29.0.post2, matching the source pin).
set(ENV{MICROPY_MPYCROSS} "${MPY_COMPONENT_ROOT}/tools/mpy_cross/mpy-cross")
set(ENV{PYTHONPATH} "${MPY_COMPONENT_ROOT}/tools:$ENV{PYTHONPATH}")

include(${MPY_COMPONENT_ROOT}/micropython.cmake)

# The MicroPython board fragment is generated into ${CMAKE_BINARY_DIR}/sdkconfig.board
# and listed first by its board cmake. Keep CsAW's own defaults last so the
# project partition table, flash size, PSRAM and console settings win.
list(APPEND SDKCONFIG_DEFAULTS
    ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults
    ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(csaw)

# Pre-flash a valid SPIFFS image so first boot does not need runtime formatting.
spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
