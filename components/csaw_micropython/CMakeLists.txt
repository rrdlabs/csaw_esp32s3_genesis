# CsAW embedded MicroPython VM (on-demand only).
#
# This component wraps the unmodified MicroPython v1.29.0 core as an ESP-IDF
# component, following the upstream "MicroPython as an IDF component" pattern
# (available from MicroPython >= 1.26).  The parent project's root CMakeLists.txt
# must set MICROPY_BOARD / MPY_BOARDS_DIR / MPY_COMPONENT_ROOT / MPY_SOURCE_ROOT /
# MPY_ESP32_ROOT and include micropython.cmake *before* project().  See
# docs/MICROPYTHON.md for the exact snippet.
set(MICROPY_DEF_COMPONENT MICROPY_ESPIDF_COMPONENT=1)

get_filename_component(MICROPY_DIR ${CMAKE_CURRENT_LIST_DIR}/vendor/micropython ABSOLUTE)
get_filename_component(MICROPY_PORT_DIR ${MICROPY_DIR}/ports/esp32 ABSOLUTE)
get_filename_component(MICROPY_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/vendor/micropython-lib ABSOLUTE)

# Leave the rest alone unless MicroPython's component integration changes.
include(${MICROPY_PORT_DIR}/esp32_common.cmake)
