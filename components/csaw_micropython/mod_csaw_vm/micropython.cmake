# CsAW user C module: C <-> MicroPython on-demand execution bridge.
add_library(usermod_csaw_vm INTERFACE)

target_sources(usermod_csaw_vm INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/csaw_vm.c
)

target_include_directories(usermod_csaw_vm INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(usermod INTERFACE usermod_csaw_vm)
