add_executable(gate12_pit_core
    gate12_pit_core.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
)

target_include_directories(gate12_pit_core PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate12_pit_core
    pico_stdlib
)

pico_enable_stdio_usb(gate12_pit_core 1)
pico_enable_stdio_uart(gate12_pit_core 0)

pico_add_extra_outputs(gate12_pit_core)
