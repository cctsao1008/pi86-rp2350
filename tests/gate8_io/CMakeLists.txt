add_executable(gate8_io
    gate8_io.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/io/io.c
)

target_include_directories(gate8_io PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate8_io
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate8_io
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate8_io 1)
pico_enable_stdio_uart(gate8_io 0)

pico_add_extra_outputs(gate8_io)
