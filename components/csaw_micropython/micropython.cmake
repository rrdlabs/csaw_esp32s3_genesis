# CsAW: MicroPython component bootstrap, adapted from the upstream reference
# micropython-idf-component (commit dc11952e94115e328e997950bfaa4fdf533d0032).
# Included from the project root CMakeLists.txt before project().
set(MICROPY_BOARD_DIR ${MPY_BOARDS_DIR}/${MICROPY_BOARD})
set(MICROPY_LIB_DIR ${MPY_COMPONENT_ROOT}/vendor/micropython-lib)
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)
set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})
include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
if(MICROPY_USER_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_USER_FROZEN_MANIFEST})
elseif(NOT MICROPY_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MPY_ESP32_ROOT}/boards/manifest.py)
endif()

configure_file(${MICROPY_BOARD_DIR}/sdkconfig.board.in ${CMAKE_BINARY_DIR}/sdkconfig.board)
